// unitron lsp: a Language Server Protocol server for asm900 sources (docs/editor.md).
//
// Every analysis is a real assembly of the project's main file (the same Assembler as
// `unitron asm`, with open editor buffers read instead of the disk), so the include tree, the
// symbols, their values and the errors are exactly the build's. On top of that, each file of the
// include tree is split into identifier tokens with their columns, which is what definition,
// references, rename and hover work on.
#include "lsp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include "assembler.hpp"
#include "util.hpp"

namespace unitron {

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* LSP_HELP =
    "usage: unitron lsp [--keywords | --script FILE]\n\n"
    "Language server (LSP over stdin/stdout) for editors; see docs/editor.md.\n"
    "The project is the workspace root's unitron.json ({\"mainFile\": \"src/main.asm\",\n"
    "\"includeDirs\": [\".\"]}), else the client's initializationOptions, else those defaults.\n\n"
    "options:\n"
    "  -h, --help  show this help message and exit\n"
    "  --keywords  print asm900's reserved words by kind, one kind per line (for the grammars)\n"
    "  --script F  read the messages from F, one JSON object per line, and print the replies one per\n"
    "              line; the current folder's file URI is written file:///ROOT (tests, debugging)\n";

static int print_keywords() {
    for (auto& kv : asm900_keywords()) {
        std::cout << kv.first << ":";
        for (auto& w : kv.second) std::cout << ' ' << w;
        std::cout << '\n';
    }
    return 0;
}

namespace {

// ------------------------------------------------------------------ transport

class Transport {
public:
    // --script: messages from a file, one JSON object per line, replies one per line; the
    // workspace's file URI is written as file:///ROOT both ways (tests).
    std::vector<std::string> script;
    size_t script_pos = 0;
    std::string root_uri;
    bool scripted = false;
    static void replace(std::string& s, const std::string& a, const std::string& b) {
        for (size_t i = 0; !a.empty() && (i = s.find(a, i)) != std::string::npos; i += b.size()) s.replace(i, a.size(), b);
    }
    Transport() {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }
    // Waits up to timeout_ms for input; false on timeout.
    bool wait(int timeout_ms) {
        if (scripted) return true;
        if (pos_ < buf_.size()) return true;
#ifdef _WIN32
        (void)timeout_ms;
        return false;   // no cheap poll on a pipe: analyze now
#else
        pollfd p{0, POLLIN, 0};
        return ::poll(&p, 1, timeout_ms) > 0;
#endif
    }
    // One message body, or nullopt at end of input.
    std::optional<std::string> read() {
        if (scripted) {
            while (script_pos < script.size()) {
                std::string l = script[script_pos++];
                if (strip(l).empty() || strip(l)[0] == '#') continue;
                replace(l, "file:///ROOT", root_uri);
                return l;
            }
            return std::nullopt;
        }
        size_t len = 0;
        bool have_len = false;
        for (;;) {
            std::string line;
            if (!read_line(line)) return std::nullopt;
            if (line.empty()) break;
            auto c = line.find(':');
            if (c != std::string::npos && lower(line.substr(0, c)) == "content-length") {
                len = std::stoul(line.substr(c + 1));
                have_len = true;
            }
        }
        if (!have_len) return std::string();
        std::string body;
        while (body.size() < len) {
            if (pos_ >= buf_.size() && !fill()) return std::nullopt;
            size_t n = std::min(len - body.size(), buf_.size() - pos_);
            body.append(buf_, pos_, n);
            pos_ += n;
        }
        return body;
    }
    void write(const json& j) {
        std::string body = j.dump(-1, ' ', false, json::error_handler_t::replace);
        if (scripted) {
            replace(body, root_uri, "file:///ROOT");
            std::cout << body << "\n";
            return;
        }
        std::string msg = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        std::fwrite(msg.data(), 1, msg.size(), stdout);
        std::fflush(stdout);
    }

private:
    std::string buf_;
    size_t pos_ = 0;
    bool fill() {
        char tmp[65536];
#ifdef _WIN32
        int n = _read(0, tmp, sizeof tmp);
#else
        ssize_t n = ::read(0, tmp, sizeof tmp);
#endif
        if (n <= 0) return false;
        buf_.erase(0, pos_);
        pos_ = 0;
        buf_.append(tmp, size_t(n));
        return true;
    }
    bool read_line(std::string& line) {
        for (;;) {
            auto e = buf_.find('\n', pos_);
            if (e != std::string::npos) {
                line = buf_.substr(pos_, e - pos_);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                pos_ = e + 1;
                return true;
            }
            if (!fill()) return false;
        }
    }
};

// ------------------------------------------------------------------ paths, URIs, columns

std::string uri_to_path(const std::string& uri) {
    std::string s = uri;
    if (s.rfind("file://", 0) == 0) s = s.substr(7);
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
#ifdef _WIN32
    if (out.size() >= 3 && out[0] == '/' && out[2] == ':') out = out.substr(1);
#endif
    return canonical_path(out);
}

std::string path_to_uri(const std::string& path) {
    std::string p = fs::u8path(path).generic_u8string();
    if (p.empty() || p[0] != '/') p = "/" + p;
    std::string out = "file://";
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (isalnum(c) || c == '/' || c == '-' || c == '.' || c == '_' || c == '~' || c == ':') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string abs_in(const std::string& root, const std::string& p) {
    fs::path q = fs::u8path(p);
    return canonical_path((q.is_absolute() ? q : fs::u8path(root) / q).u8string());
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') i++;
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);
    return lines;
}

// LSP columns count UTF-16 code units; these convert from and to byte offsets in a UTF-8 line.
size_t utf8_len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }
int utf16_col(const std::string& line, size_t byte) {
    int col = 0;
    for (size_t i = 0; i < byte && i < line.size();) {
        size_t n = utf8_len(line[i]);
        col += n == 4 ? 2 : 1;
        i += n;
    }
    return col;
}
size_t byte_col(const std::string& line, int utf16) {
    int col = 0;
    size_t i = 0;
    while (i < line.size() && col < utf16) {
        size_t n = utf8_len(line[i]);
        col += n == 4 ? 2 : 1;
        i += n;
    }
    return i;
}

bool ident_start(char c) { return isalpha((unsigned char)c) || c == '_' || c == '.'; }
bool ident_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '\''; }

std::string hex_of(int64_t v) {
    std::ostringstream s;
    s << "0x" << std::uppercase << std::hex << (v & 0xFFFFFFFFLL);
    return s.str();
}

// ------------------------------------------------------------------ the index

struct Tok {
    std::string name;
    int line;        // 0-based
    size_t b0, b1;   // byte columns
};

struct Link {
    int line;
    size_t b0, b1;       // the path inside the quotes
    std::string target;  // canonical path, empty when not found
};

struct FileInfo {
    std::vector<std::string> lines;
    std::vector<Tok> toks;
    std::vector<Link> links;
};

struct Def {
    std::string name, kind;   // label, equ, macro, define
    std::string file;         // canonical path
    int line = 0;
    size_t b0 = 0, b1 = 0;
    std::string section;
    bool has_value = false;
    int64_t value = 0;
};

struct Diag {
    int line;
    int severity;   // 1 error, 2 warning
    std::string msg;
};

int symbol_kind(const std::string& k) {
    if (k == "label") return 12;   // Function
    if (k == "macro") return 6;    // Method
    return 14;                     // Constant (equ, #define)
}

class Server {
public:
    int run();
    Transport& io() { return io_; }

private:
    Transport io_;
    std::string root_;
    std::string main_file_ = "src/main.asm";
    std::vector<std::string> include_dirs_{"."};
    std::map<std::string, std::string> overlay_;   // canonical path -> open buffer
    bool initialized_ = false, dirty_ = true, shutdown_ = false;
    std::map<std::string, FileInfo> files_;
    std::multimap<std::string, Def> defs_;
    std::map<std::string, std::vector<Diag>> diags_;
    std::set<std::string> published_;
    std::string note_;

    void handle(const json& msg);
    void reply(const json& id, const json& result) { io_.write({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}); }
    void reply_error(const json& id, int code, const std::string& m) {
        io_.write({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", m}}}});
    }
    void notify(const std::string& method, const json& params) {
        io_.write({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
    }
    void log(const std::string& m) { notify("window/logMessage", {{"type", 3}, {"message", m}}); }

    void configure(const json& init_opts);
    void analyze();
    void ensure() {
        if (dirty_) analyze();
    }
    void publish();
    void index_file(const std::string& path);
    std::string resolve(const std::string& name, const std::string& cur);

    json range(const std::string& file, int line, size_t b0, size_t b1);
    json location(const std::string& file, int line, size_t b0, size_t b1) {
        return {{"uri", path_to_uri(file)}, {"range", range(file, line, b0, b1)}};
    }
    const Tok* tok_at(const std::string& file, const json& pos);
    const Link* link_at(const std::string& file, const json& pos);
    std::string hover_text(const Def& d);
    std::string rel(const std::string& p) {
        return fs::u8path(p).lexically_relative(fs::u8path(root_)).generic_u8string();
    }
};

void Server::configure(const json& opts) {
    auto take = [this](const json& j) {
        if (!j.is_object()) return;
        if (j.contains("mainFile") && j["mainFile"].is_string() && !j["mainFile"].get<std::string>().empty())
            main_file_ = j["mainFile"].get<std::string>();
        if (j.contains("includeDirs") && j["includeDirs"].is_array()) {
            include_dirs_.clear();
            for (auto& d : j["includeDirs"])
                if (d.is_string()) include_dirs_.push_back(d.get<std::string>());
        }
    };
    take(opts);
    std::ifstream f(fs::u8path(root_) / "unitron.json");
    if (f) {
        try {
            take(json::parse(f));
        } catch (std::exception& e) {
            note_ = std::string("unitron.json: ") + e.what();
        }
    }
}

// An include path as the assembler looks it up: the including file's folder, then -I folders.
std::string Server::resolve(const std::string& name, const std::string& cur) {
    std::vector<fs::path> dirs{fs::u8path(cur).parent_path()};
    for (auto& d : include_dirs_) dirs.push_back(fs::u8path(root_) / fs::u8path(d));
    if (fs::u8path(name).is_absolute()) dirs = {fs::path()};
    for (auto& d : dirs) {
        std::error_code ec;
        fs::path p = d.empty() ? fs::u8path(name) : d / fs::u8path(name);
        std::string c = canonical_path(p.u8string());
        if (fs::is_regular_file(p, ec) || overlay_.count(c)) return c;
    }
    return "";
}

void Server::index_file(const std::string& path) {
    FileInfo fi;
    std::string text;
    auto it = overlay_.find(path);
    if (it != overlay_.end()) {
        text = it->second;
    } else {
        Bytes b;
        if (!read_bytes_opt(path, b)) return;
        text.assign(b.begin(), b.end());
    }
    fi.lines = split_lines(text);
    static const std::regex inc(R"(^\s*(\$include|\$charmap|#\s*include)\s+["<]([^">]*)[">])", std::regex::icase);
    static const std::regex macro(R"(^\s*\?macro\s+([A-Za-z_][\w.]*))", std::regex::icase);
    static const std::regex define(R"(^\s*#\s*define\s+([A-Za-z_]\w*))");
    bool block = false;   // inside /* */
    for (int ln = 0; ln < int(fi.lines.size()); ln++) {
        const std::string& s = fi.lines[ln];
        std::smatch m;
        if (std::regex_search(s, m, inc)) {
            fi.links.push_back({ln, size_t(m.position(2)), size_t(m.position(2) + m.length(2)), resolve(m.str(2), path)});
            continue;
        }
        bool is_macro = std::regex_search(s, m, macro);
        if (is_macro || std::regex_search(s, m, define)) {
            Def d;
            d.name = m.str(1);
            d.kind = is_macro ? "macro" : "define";
            d.file = path;
            d.line = ln;
            d.b0 = size_t(m.position(1));
            d.b1 = d.b0 + d.name.size();
            defs_.emplace(d.name, d);
        }
        // identifier tokens outside comments, strings and char constants
        for (size_t i = 0; i < s.size();) {
            char c = s[i];
            if (block) {
                auto e = s.find("*/", i);
                if (e == std::string::npos) break;
                block = false;
                i = e + 2;
                continue;
            }
            if (c == ';' || (c == '/' && i + 1 < s.size() && s[i + 1] == '/')) break;
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                block = true;
                i += 2;
                continue;
            }
            if (c == '"') {
                for (i++; i < s.size(); i++) {
                    if (s[i] == '"' && i + 1 < s.size() && s[i + 1] == '"') i++;
                    else if (s[i] == '"') break;
                }
                i++;
                continue;
            }
            if (c == '\'') {   // a char constant; a ' right after an identifier is part of it (below)
                auto e = s.find('\'', i + 1);
                i = e == std::string::npos ? s.size() : e + 1;
                continue;
            }
            if (c == '$' || c == '?' || c == '#' || isdigit((unsigned char)c)) {   // controls, macro words, numbers
                for (i++; i < s.size() && ident_char(s[i]); i++) {
                }
                continue;
            }
            if (ident_start(c)) {
                size_t b = i;
                while (i < s.size() && ident_char(s[i])) i++;
                if (i - b > 1 || c != '.') fi.toks.push_back({s.substr(b, i - b), ln, b, i});
                continue;
            }
            i++;
        }
    }
    files_[path] = std::move(fi);
}

void Server::analyze() {
    dirty_ = false;
    files_.clear();
    defs_.clear();
    diags_.clear();
    std::error_code ec;
    fs::current_path(fs::u8path(root_), ec);
    set_read_overlay(&overlay_);
    std::string main_abs = abs_in(root_, main_file_);
    Assembler as(include_dirs_);
    as.track_deps(true, false);
    bool assembled = false;
    try {
        as.add_module(main_file_);
        if (!as.missing) {
            as.assemble(true);
            assembled = true;
        }
    } catch (std::exception& e) {
        as.d.error(Where::None(), 0, e.what());
    }
    set_read_overlay(nullptr);
    // diagnostics: "file:line: error E12: message" (+ "included from" lines)
    static const std::regex head(R"(^(.*?):(\d+): (error|warning)(?: [EW]\d+)?: (.*)$)");
    auto add = [&](const std::string& text, int sev) {
        std::string first = text.substr(0, text.find('\n'));
        std::smatch m;
        if (std::regex_match(first, m, head)) {
            diags_[abs_in(root_, m.str(1))].push_back({std::max(0, std::stoi(m.str(2)) - 1), sev, m.str(4)});
        } else {
            for (auto p : {"error: ", "warning: "})
                if (first.rfind(p, 0) == 0) first = first.substr(strlen(p));
            diags_[main_abs].push_back({0, sev, first});
        }
    };
    for (auto& e : as.d.errors) add(e, 1);
    for (auto& w : as.d.warnings) add(w, 2);
    if (!fs::is_regular_file(fs::u8path(main_abs), ec) && !overlay_.count(main_abs))
        log("main file " + main_file_ + " not found; set mainFile in unitron.json");
    if (as.missing)
        log("some $include files don't exist yet (made by the build?); build once to get values and every error");
    if (!note_.empty()) log(note_);
    // the files of the include tree, then their tokens
    std::vector<std::string> files{main_abs};
    for (auto& d : as.deps) files.push_back(abs_in(root_, d));
    for (auto& f : files)
        if (!files_.count(f)) index_file(f);
    // the assembler's symbols, placed on their name in the defining line
    for (auto& s : as.symbol_defs()) {
        if (s.where.none) continue;
        Def d;
        d.name = s.name;
        d.kind = s.label ? "label" : "equ";
        d.file = abs_in(root_, s.where.file);
        d.line = std::max(0, s.where.line - 1);
        d.section = s.section;
        d.has_value = s.has_value && assembled;
        d.value = s.value;
        auto fit = files_.find(d.file);
        if (fit != files_.end())
            for (auto& t : fit->second.toks)
                if (t.line == d.line && t.name == d.name) {
                    d.b0 = t.b0;
                    d.b1 = t.b1;
                    break;
                }
        defs_.emplace(d.name, d);
    }
    publish();
}

void Server::publish() {
    std::set<std::string> now;
    for (auto& kv : diags_) {
        json arr = json::array();
        auto fit = files_.find(kv.first);
        for (auto& d : kv.second) {
            size_t len = 0;
            if (fit != files_.end() && d.line < int(fit->second.lines.size())) len = fit->second.lines[d.line].size();
            arr.push_back({{"range", range(kv.first, d.line, 0, len)},
                           {"severity", d.severity},
                           {"source", "unitron"},
                           {"message", d.msg}});
        }
        notify("textDocument/publishDiagnostics", {{"uri", path_to_uri(kv.first)}, {"diagnostics", arr}});
        now.insert(kv.first);
    }
    for (auto& f : published_)
        if (!now.count(f)) notify("textDocument/publishDiagnostics", {{"uri", path_to_uri(f)}, {"diagnostics", json::array()}});
    published_ = now;
}

json Server::range(const std::string& file, int line, size_t b0, size_t b1) {
    std::string text;
    auto fit = files_.find(file);
    if (fit != files_.end() && line < int(fit->second.lines.size())) text = fit->second.lines[line];
    return {{"start", {{"line", line}, {"character", utf16_col(text, b0)}}},
            {"end", {{"line", line}, {"character", utf16_col(text, b1)}}}};
}

const Tok* Server::tok_at(const std::string& file, const json& pos) {
    auto fit = files_.find(file);
    if (fit == files_.end()) return nullptr;
    int line = pos.value("line", 0);
    if (line < 0 || line >= int(fit->second.lines.size())) return nullptr;
    size_t b = byte_col(fit->second.lines[line], pos.value("character", 0));
    for (auto& t : fit->second.toks)
        if (t.line == line && b >= t.b0 && b <= t.b1) return &t;
    return nullptr;
}

const Link* Server::link_at(const std::string& file, const json& pos) {
    auto fit = files_.find(file);
    if (fit == files_.end()) return nullptr;
    int line = pos.value("line", 0);
    if (line < 0 || line >= int(fit->second.lines.size())) return nullptr;
    size_t b = byte_col(fit->second.lines[line], pos.value("character", 0));
    for (auto& l : fit->second.links)
        if (l.line == line && b >= l.b0 && b <= l.b1) return &l;
    return nullptr;
}

std::string Server::hover_text(const Def& d) {
    auto fit = files_.find(d.file);
    std::string src;
    if (fit != files_.end() && d.line < int(fit->second.lines.size())) src = fit->second.lines[d.line];
    std::string code = strip(split_comment(src));
    std::string md = "```asm900\n" + (code.empty() ? d.name : code) + "\n```\n";
    std::string what = d.kind;
    if (d.has_value) {
        if (d.kind == "label")
            what += " at " + hex_of(d.value);
        else
            what += " = " + hex_of(d.value) + " (" + std::to_string(d.value) + ")";
    }
    if (!d.section.empty()) what += ", section `" + d.section + "`";
    md += what + ", " + rel(d.file) + ":" + std::to_string(d.line + 1) + "\n";
    // the comment block just above the definition, and the comment on its line
    std::vector<std::string> doc;
    if (fit != files_.end()) {
        for (int l = d.line - 1; l >= 0 && l >= d.line - 40; l--) {
            std::string t = strip(fit->second.lines[l]);
            if (t.empty() || t[0] != ';') break;
            doc.insert(doc.begin(), strip(t.substr(1)));
        }
        auto c = src.find(';');
        if (c != std::string::npos && !code.empty()) doc.push_back(strip(src.substr(c + 1)));
    }
    if (!doc.empty()) {
        md += "\n---\n\n";
        for (auto& l : doc) md += l + "  \n";
    }
    return md;
}

void Server::handle(const json& msg) {
    std::string method = msg.value("method", "");
    if (method.empty()) return;   // a response to a request of ours: none are sent
    json id = msg.contains("id") ? msg["id"] : json();
    json params = msg.contains("params") ? msg["params"] : json::object();
    bool is_request = msg.contains("id");
    try {
        if (method == "initialize") {
            std::string root;
            if (params.contains("workspaceFolders") && params["workspaceFolders"].is_array() &&
                !params["workspaceFolders"].empty())
                root = uri_to_path(params["workspaceFolders"][0].value("uri", ""));
            else if (params.contains("rootUri") && params["rootUri"].is_string())
                root = uri_to_path(params["rootUri"]);
            else if (params.contains("rootPath") && params["rootPath"].is_string())
                root = canonical_path(params["rootPath"]);
            std::error_code ec;
            root_ = root.empty() ? fs::current_path(ec).u8string() : root;
            configure(params.contains("initializationOptions") ? params["initializationOptions"] : json::object());
            reply(id, {{"capabilities",
                        {{"textDocumentSync", {{"openClose", true}, {"change", 1}, {"save", {{"includeText", false}}}}},
                         {"definitionProvider", true},
                         {"referencesProvider", true},
                         {"renameProvider", {{"prepareProvider", true}}},
                         {"hoverProvider", true},
                         {"documentSymbolProvider", true},
                         {"workspaceSymbolProvider", true},
                         {"documentLinkProvider", {{"resolveProvider", false}}}}},
                       {"serverInfo", {{"name", "unitron lsp"}}}});
        } else if (method == "initialized") {
            initialized_ = true;
            analyze();
        } else if (method == "shutdown") {
            shutdown_ = true;
            reply(id, nullptr);
        } else if (method == "exit") {
            std::exit(shutdown_ ? 0 : 1);
        } else if (method == "textDocument/didOpen") {
            auto& td = params["textDocument"];
            overlay_[uri_to_path(td["uri"])] = td.value("text", "");
            dirty_ = true;
        } else if (method == "textDocument/didChange") {
            auto& changes = params["contentChanges"];
            if (changes.is_array() && !changes.empty())
                overlay_[uri_to_path(params["textDocument"]["uri"])] = changes.back().value("text", "");
            dirty_ = true;
        } else if (method == "textDocument/didClose") {
            overlay_.erase(uri_to_path(params["textDocument"]["uri"]));
            dirty_ = true;
        } else if (method == "textDocument/didSave" || method == "workspace/didChangeWatchedFiles") {
            dirty_ = true;
        } else if (method == "textDocument/definition") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            json out = json::array();
            if (auto* l = link_at(file, params["position"])) {
                if (!l->target.empty()) out.push_back({{"uri", path_to_uri(l->target)},
                                                       {"range", {{"start", {{"line", 0}, {"character", 0}}},
                                                                  {"end", {{"line", 0}, {"character", 0}}}}}});
            } else if (auto* t = tok_at(file, params["position"])) {
                auto r = defs_.equal_range(t->name);
                for (auto it = r.first; it != r.second; ++it)
                    out.push_back(location(it->second.file, it->second.line, it->second.b0, it->second.b1));
            }
            reply(id, out);
        } else if (method == "textDocument/references") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            json out = json::array();
            if (auto* t = tok_at(file, params["position"])) {
                std::string name = t->name;
                bool decl = true;
                if (params.contains("context") && params["context"].is_object())
                    decl = params["context"].value("includeDeclaration", true);
                std::set<std::pair<std::string, int>> defs;
                auto r = defs_.equal_range(name);
                for (auto it = r.first; it != r.second; ++it) defs.insert({it->second.file, it->second.line});
                if (r.first != r.second)
                    for (auto& fv : files_)
                        for (auto& tk : fv.second.toks)
                            if (tk.name == name && (decl || !defs.count({fv.first, tk.line})))
                                out.push_back(location(fv.first, tk.line, tk.b0, tk.b1));
            }
            reply(id, out);
        } else if (method == "textDocument/prepareRename") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            const Tok* t = tok_at(file, params["position"]);
            if (t && defs_.count(t->name))
                reply(id, {{"range", range(file, t->line, t->b0, t->b1)}, {"placeholder", t->name}});
            else
                reply(id, nullptr);
        } else if (method == "textDocument/rename") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            std::string nn = params.value("newName", "");
            const Tok* t = tok_at(file, params["position"]);
            if (!t || !defs_.count(t->name)) {
                reply_error(id, -32602, "not a symbol defined in the project");
                return;
            }
            bool ok = !nn.empty() && ident_start(nn[0]);
            for (char c : nn) ok = ok && ident_char(c);
            if (!ok || is_reserved(nn)) {
                reply_error(id, -32602, "'" + nn + "' is not a valid asm900 symbol name");
                return;
            }
            if (defs_.count(nn)) {
                reply_error(id, -32602, "'" + nn + "' is already defined");
                return;
            }
            std::string old = t->name;
            json changes = json::object();
            for (auto& fv : files_) {
                json edits = json::array();
                for (auto& tk : fv.second.toks)
                    if (tk.name == old) edits.push_back({{"range", range(fv.first, tk.line, tk.b0, tk.b1)}, {"newText", nn}});
                if (!edits.empty()) changes[path_to_uri(fv.first)] = edits;
            }
            reply(id, {{"changes", changes}});
        } else if (method == "textDocument/hover") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            json out = nullptr;
            if (auto* l = link_at(file, params["position"])) {
                std::string where = l->target.empty() ? "not found (made by the build?)" : "`" + rel(l->target) + "`";
                out = {{"contents", {{"kind", "markdown"}, {"value", where}}}};
            } else if (auto* t = tok_at(file, params["position"])) {
                std::string md;
                auto r = defs_.equal_range(t->name);
                for (auto it = r.first; it != r.second; ++it) md += (md.empty() ? "" : "\n---\n") + hover_text(it->second);
                if (!md.empty())
                    out = {{"contents", {{"kind", "markdown"}, {"value", md}}}, {"range", range(file, t->line, t->b0, t->b1)}};
            }
            reply(id, out);
        } else if (method == "textDocument/documentSymbol") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            std::vector<const Def*> ds;
            for (auto& kv : defs_)
                if (kv.second.file == file) ds.push_back(&kv.second);
            std::sort(ds.begin(), ds.end(), [](auto* a, auto* b) { return a->line < b->line; });
            json out = json::array();
            for (auto* d : ds)
                out.push_back({{"name", d->name}, {"kind", symbol_kind(d->kind)}, {"location", location(d->file, d->line, d->b0, d->b1)}});
            reply(id, out);
        } else if (method == "workspace/symbol") {
            ensure();
            std::string q = lower(params.value("query", ""));
            json out = json::array();
            for (auto& kv : defs_) {
                if (!q.empty() && lower(kv.first).find(q) == std::string::npos) continue;
                auto& d = kv.second;
                out.push_back({{"name", d.name}, {"kind", symbol_kind(d.kind)}, {"location", location(d.file, d.line, d.b0, d.b1)}});
                if (out.size() >= 500) break;
            }
            reply(id, out);
        } else if (method == "textDocument/documentLink") {
            ensure();
            std::string file = uri_to_path(params["textDocument"]["uri"]);
            json out = json::array();
            auto fit = files_.find(file);
            if (fit != files_.end())
                for (auto& l : fit->second.links)
                    if (!l.target.empty())
                        out.push_back({{"range", range(file, l.line, l.b0, l.b1)}, {"target", path_to_uri(l.target)}});
            reply(id, out);
        } else if (is_request) {
            reply_error(id, -32601, "method not found: " + method);
        }
    } catch (std::exception& e) {
        if (is_request) reply_error(id, -32603, e.what());
    }
}

int Server::run() {
    for (;;) {
        // after edits, analyze once the client has been quiet for a moment
        if (initialized_ && dirty_ && !io_.wait(300)) {
            analyze();
            continue;
        }
        auto body = io_.read();
        if (!body) return shutdown_ || io_.scripted ? 0 : 1;
        if (body->empty()) continue;
        json msg;
        try {
            msg = json::parse(*body);
        } catch (std::exception& e) {
            io_.write({{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", e.what()}}}});
            continue;
        }
        handle(msg);
    }
}

}  // namespace

int cmd_lsp(const std::vector<std::string>& args) {
    if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
        std::cout << LSP_HELP;
        return 0;
    }
    if (!args.empty() && args[0] == "--keywords") return print_keywords();
    Server s;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--stdio") continue;   // clients often pass it; stdio is the only transport
        if (args[i] == "--script" && i + 1 < args.size()) {
            Bytes b;
            if (!read_bytes_opt(args[i + 1], b)) {
                std::cerr << "unitron lsp: error: can't open " << args[i + 1] << "\n";
                return 1;
            }
            auto& t = s.io();
            t.scripted = true;
            t.script = split_lines(std::string(b.begin(), b.end()));
            std::error_code ec;
            t.root_uri = path_to_uri(canonical_path(fs::current_path(ec).u8string()));
            i++;
            continue;
        }
        std::cerr << "unitron lsp: error: unknown argument " << args[i] << "\n";
        return 2;
    }
    return s.run();
}

}  // namespace unitron

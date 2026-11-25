import * as vscode from "vscode";
import { spawn } from "child_process";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;
let output: vscode.OutputChannel | undefined;
let failureShown = false;

function config() {
  const c = vscode.workspace.getConfiguration("unitron");
  return {
    path: (c.get<string>("path", "unitron") ?? "").trim() || "unitron",
    mainFile: (c.get<string>("mainFile", "src/main.asm") ?? "").trim() || "src/main.asm",
    includeDirs: c.get<string[]>("includeDirs", ["."]),
  };
}

function workspaceRoot(): string | undefined {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

/** Resolves when `<path> lsp --help` runs; rejects with a reason otherwise. */
export function probe(path: string, cwd?: string): Promise<void> {
  return new Promise((resolve, reject) => {
    let done = false;
    let timer: NodeJS.Timeout | undefined;
    const finish = (err?: Error) => {
      if (done) return;
      done = true;
      if (timer) clearTimeout(timer);
      err ? reject(err) : resolve();
    };
    let p;
    try {
      p = spawn(path, ["lsp", "--help"], { cwd, stdio: "ignore" });
    } catch (e) {
      finish(e as Error);
      return;
    }
    p.on("error", (e) => finish(e));
    p.on("exit", (code) =>
      code === 0 ? finish() : finish(new Error(`'${path} lsp --help' exited with code ${code}`))
    );
    timer = setTimeout(() => {
      p.kill();
      finish(new Error(`'${path} lsp --help' did not finish`));
    }, 5000);
  });
}

async function reportFailure(reason: string) {
  output?.appendLine(`unitron lsp not started: ${reason}`);
  if (failureShown) return;
  failureShown = true;
  const open = "Open Settings";
  const pick = await vscode.window.showErrorMessage(
    `unitron language server could not start (${reason}). Colouring still works. Set unitron.path to the unitron binary.`,
    open
  );
  if (pick === open) {
    vscode.commands.executeCommand("workbench.action.openSettings", "unitron.path");
  }
}

async function start(context: vscode.ExtensionContext) {
  const cfg = config();
  const cwd = workspaceRoot();
  try {
    await probe(cfg.path, cwd);
  } catch (e) {
    const err = e as NodeJS.ErrnoException;
    await reportFailure(
      err.code === "ENOENT" ? `'${cfg.path}' not found` : err.message
    );
    return;
  }

  const serverOptions: ServerOptions = {
    command: cfg.path,
    args: ["lsp"],
    transport: TransportKind.stdio,
    options: { cwd },
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "asm900" }],
    initializationOptions: { mainFile: cfg.mainFile, includeDirs: cfg.includeDirs },
    outputChannel: output,
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/unitron.json"),
    },
  };
  // The id "unitron" makes vscode-languageclient read unitron.trace.server.
  client = new LanguageClient("unitron", "unitron", serverOptions, clientOptions);
  try {
    await client.start();
    failureShown = false;
  } catch (e) {
    client = undefined;
    await reportFailure((e as Error).message ?? String(e));
  }
}

async function stop() {
  const c = client;
  client = undefined;
  if (c) {
    try {
      await c.stop();
    } catch {
      /* already gone */
    }
  }
}

export async function activate(context: vscode.ExtensionContext) {
  output = vscode.window.createOutputChannel("unitron");
  context.subscriptions.push(
    output,
    vscode.commands.registerCommand("unitron.restartServer", async () => {
      failureShown = false;
      await stop();
      await start(context);
    }),
    vscode.commands.registerCommand("unitron.showOutput", () => output?.show(true)),
    vscode.workspace.onDidChangeConfiguration(async (e) => {
      if (
        e.affectsConfiguration("unitron.path") ||
        e.affectsConfiguration("unitron.mainFile") ||
        e.affectsConfiguration("unitron.includeDirs")
      ) {
        failureShown = false;
        await stop();
        await start(context);
      }
    })
  );
  await start(context);
}

export function deactivate(): Thenable<void> | undefined {
  return stop();
}

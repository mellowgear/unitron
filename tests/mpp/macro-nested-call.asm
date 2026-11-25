?macro inner x
	db	?x
?endm
?macro outer y
	inner	?y
	inner	?y+1
?endm
	outer	5

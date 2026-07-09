package main



func cmdRun(args []string) {
	// strip leading "run" if present (backward compat)
	if len(args) > 0 && args[0] == "run" {
		args = args[1:]
	}
	if len(args) == 1 && (args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
		showHelp()
		return
	}
	mode, name, rc := parseInput(args, "run")
	switch mode {
	case modeServe:
		runServe()
	case modeFile:
		runFile(args)
	case modeInline, modePipe:
		runCode(name, rc)
	}
}

// ── serve mode: kvlang run (no args) ──






// ── one-shot: kvlang run <file.kv> ──

// registerDefaultTerm 注册默认终端，使 print/cerr/input 输出到当前进程。


// ── one-shot: kvlang run -c "code" / echo "code" | kvlang run ──


// ── helpers ──




# Token-saving tools

Installed by `SETUP-TOKEN-SAVERS.bat` (from Eric Tech's video "4 Free Repos
That Cut Claude Code Token Usage"). All free and open source; they run on this
PC.

| Tool | What it does | Check it | Turn it off |
|---|---|---|---|
| [RTK](https://github.com/rtk-ai/rtk) | A hook rewrites shell commands (`git status`, `rg`, `ls`, test/build tools …) through `rtk`, which cuts noise from the output before Claude reads it. Only affects shell commands, not Claude's own Read/Grep tools. Telemetry is off by default. | `rtk gain` | `rtk init -g --uninstall` |
| [Ponytail](https://github.com/DietrichGebert/ponytail) | Claude Code plugin with an always-on "write the least code that works" rule set, so replies and diffs are smaller. | `/ponytail-gain` | `/ponytail off` (session), `/plugin remove ponytail` |
| [Graphify](https://github.com/Graphify-Labs/graphify) | Turns the code into a map in `graphify-out/` (built locally with tree-sitter, no AI). Claude asks the map ("where is the 3D camera set up?") instead of reading many files. `.graphifyignore` keeps generated code, third-party code and documents out. | `graphify query "where is field_camera used"` | delete `.claude/skills/graphify` |

Keep the map current after big changes: `graphify update .` (or `/graphify . --update`
in Claude Code).

## Why Headroom isn't installed

Headroom sits as a proxy between Claude Code and Anthropic and compresses the
conversation. It's the heaviest of the four (Python + Docker recommended on
Windows, AVX2 CPU), it's aimed at API-key setups with large JSON/log payloads,
and rewriting the conversation can stop Claude Code's own prompt cache from
working, which already makes repeated context about 90% cheaper. For this
project it's more risk than gain. It can be added later if usage is still a
problem.

## Ponytail and this project

Ponytail pushes for minimal code. Guest-memory reads still need their validity
checks (`Mem::valid`, null pointers) and features still need their settings
toggle: those are requirements, not over-engineering. If it cuts too much, use
`/ponytail lite`.

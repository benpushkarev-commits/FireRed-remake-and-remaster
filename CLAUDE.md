# CLAUDE.md

@AGENTS.md

## Claude Code notes

- `.claude/settings.json` blocks reading generated code, `build/`, `deps/`,
  and the font blob. The GitHub repo is public: never commit copyrighted game files (see AGENTS.md).
  GitHub command restrictions were removed.
- Engine and game repos have their own deeper notes
  (`FireRedLeafGreenRecomp/CLAUDE.md`, `FireRedLeafGreenRecomp/gbarecomp/CLAUDE.md`).
  Only open them when working on the engine or the recompiler itself; remaster
  work (everything in `FireRedLeafGreenRecomp/src/fr_*`) doesn't need them.
- Prefer Sonnet for routine work to save usage (`/model sonnet`); switch to
  `/model opus` for hard 3D/engine problems.
- Use `/compact` when a session gets long, and `docs/STATE.md` as the hand-off
  note for the next session.

# Compact instructions

When compacting, keep: files changed and why, current build/test status,
open problems, and Ben's rules. Drop old tool output and screenshots.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

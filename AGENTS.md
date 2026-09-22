# Project instructions

- Keep repository content in English and use Conventional Commits.
- Record every change a player would notice in `CHANGELOG.md`, under `Unreleased`, in the same
  change. Describe the behaviour, not the patch.
- Credit only human authors. Do not name an assistant, agent, or generator anywhere in the repository: not in commit messages or trailers, not in pull request descriptions, not in code comments, documentation, or contributor lists. Never add a `Co-Authored-By` line or a "generated with" note for a tool.
- Before taking screenshots, move the pointer outside the captured game or preview content. Do not include the agent's cursor in published screenshots. Verify the captured result before saving it as an artifact.
- Keep player-facing UI focused on rooms, depth, navigation, and actionable errors. Do not display renderer names, engine details, implementation status, or debugging labels during normal play.
- Derive water conditions from game state. Do not add manual wet/dry controls to the player interface.
- Open previews must refresh when their source state changes. Read existing outer-room instances; ordinary chest destinations must follow fresh-entry semantics and saved global state. Close invalid previews instead of retaining stale object pointers.
- Test against the isolated runtime and profile. A test that has to use real Steam progress backs it
  up first with `tools/backup-saves.ps1` and says so in the validation notes. Preserve save backups and
  keep game files, saves, dependencies, and build artifacts out of version control.
- After interactive testing, close the isolated game and its preview windows and verify that the test process has exited.

# Publish Muse Vita to GitHub and work in Codex

Owner: Sam Dyatt (`@msdyatt`)  
Proposed repository: `https://github.com/msdyatt/muse-vita`

**Note:** This package does not create the GitHub repository automatically.

## One-time setup on your Mac

1. Extract the `muse-vita-codex-ready.zip` archive.
2. Install GitHub CLI (`brew install gh`, if needed) and sign in using `gh auth login`.
3. In Terminal, `cd` into the **extracted project folder** (the one containing `README.md`).
4. Run:

```sh
git init -b main
git add .
git commit -m "Initial Muse Vita prototype and Codex handoff"
gh repo create msdyatt/muse-vita --public --source=. --remote=origin --push
```

If `msdyatt/muse-vita` already exists, **do not run `gh repo create`**:
check the remote first and reconcile its files rather than force-pushing.

Choose private instead of public on initial creation if you want to review
Meta's Gadget SDK terms and distribution limitations before making it public.
The planned public project should never bundle any real Muse tokens.

## Continue in Codex

Open Codex in the ChatGPT desktop application or Codex CLI, choose the
local cloned/extracted repository, and ask:

> Please follow AGENTS.md and implement CODEX_START_HERE.md in a
> `codex/vita-ui-mvp` branch. Build/test what you can, and make a PR.
> Don't describe network stubs as working Muse connections.

With the published repo, Codex can create branches/PRs when given GitHub
permissions. The `AGENTS.md` file remains inside the repository so future
coding sessions retain the technical and security requirements.

## Check publishing succeeded

```sh
git remote -v
git log -1 --oneline
gh repo view msdyatt/muse-vita --web
```

Confirm README, LICENSE, AGENTS.md, CODEX_START_HERE.md, and `.github/workflows`
are visible on GitHub. CI runs Python tests only; it does not verify a Vita VPK
build until a VitaSDK runner is configured.

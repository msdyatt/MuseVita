# Publish MuseVita to GitHub and work through pull requests

Owner: Sam Dyatt (`@msdyatt`)  
Proposed repository: `https://github.com/msdyatt/MuseVita`

**Note:** This package does not create the GitHub repository automatically.

## One-time setup on your Mac

1. Use the extracted MuseVita project folder.
2. Install GitHub CLI (`brew install gh`, if needed) and sign in using `gh auth login`.
3. In Terminal, `cd` into the **extracted project folder** (the one containing `README.md`).
4. Run:

```sh
git init -b main
git add .
git commit -m "Initial MuseVita prototype"
gh repo create msdyatt/MuseVita --public --source=. --remote=origin --push
```

If `msdyatt/MuseVita` already exists, **do not run `gh repo create`**:
check the remote first and reconcile its files rather than force-pushing.

Choose private instead of public on initial creation if you want to review
Meta's Gadget SDK terms and distribution limitations before making it public.
The planned public project should never bundle any real Muse tokens.

## Continue development

Choose the local repository, create a feature branch, and ask the coding agent:

> Please follow AGENTS.md and implement CODEX_START_HERE.md in a
> `feature/vita-ui-mvp` branch. Build/test what you can, and make a PR.
> Don't describe network stubs as working Muse connections.

The `AGENTS.md` file remains inside the repository so future coding sessions
retain the technical and security requirements.

## Check publishing succeeded

```sh
git remote -v
git log -1 --oneline
gh repo view msdyatt/MuseVita --web
```

Confirm README, LICENSE, AGENTS.md, CODEX_START_HERE.md, and `.github/workflows`
are visible on GitHub. CI runs Python tests only; it does not verify a Vita VPK
build until a VitaSDK runner is configured.

# Git hooks

Repository-tracked hooks live here. Enable them once per checkout:

```
git config core.hooksPath .githooks
```

## `pre-commit`

Refuses to commit `include/secrets.h` from any viewer or tool. The
file holds private Wi-Fi and provider credentials, is covered by
`.gitignore`, and never belongs in version control. The hook is a
belt-and-braces backstop for the case where someone forces a stage
with `git add -f` or edits an already-tracked copy from an older
checkout.

Bypass with `git commit --no-verify` only when you are absolutely
sure the staged file contains no real credentials.

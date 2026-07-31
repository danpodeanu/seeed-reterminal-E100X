# Git hooks

Repository-tracked hooks live here. Enable them once per checkout:

```
git config core.hooksPath .githooks
```

## `pre-commit`

Refuses to commit `include/secrets.h` (in any viewer or tool) when it no
longer matches its sibling `include/secrets.h.example`. This is the
guardrail that keeps real Wi-Fi and QWeather credentials from being
committed accidentally now that `secrets.h` is tracked by git.

Bypass with `git commit --no-verify` when you're intentionally reshaping
the template -- remember to update the `.example` in the same commit so
the hook stops complaining afterwards.

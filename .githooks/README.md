# Git hooks

Repository-tracked hooks live here. Enable them once per checkout:

```
git config core.hooksPath .githooks
```

## `pre-commit`

Allows the tracked mock `include/secrets.h` files for XKCD, weather,
and photo viewer only while each is byte-for-byte identical to its
staged `secrets.h.example`. It rejects customized viewer credentials
and every other `include/secrets.h`.

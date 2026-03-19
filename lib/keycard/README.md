# libkeycard.so - Backup Binary

**Status**: Backup/fallback binary (as of 2026-03-19)

## Current Build Method

As of issue #44, libkeycard.so is **built from source** in `flake.nix`:
```nix
libkeycard = pkgs.buildGoModule {
  src = pkgs.fetchFromGitHub {
    owner = "status-im";
    repo = "status-keycard-go";
    rev = "76c880480c62dbf0ee67ee342f87ab80a928ed73";
  };
  # ... builds with CGO + pcsclite
};
```

## Why This Binary Exists

This pre-built binary is kept as a **safety net** in case:
- Nix build breaks in the future
- Quick fallback needed during development
- Cross-compilation issues arise

## Version Info

- **Source**: status-keycard-go commit `76c8804`
- **Built**: 2024-03-15 (see git log)
- **Size**: ~14MB
- **Build command**: `./scripts/build-libkeycard.sh`

## When to Remove

After several months of stable Nix builds + verified AppImage functionality, this backup can be removed to reduce repo size.

## Related

- Issue #44: Build libkeycard from source
- Branch: `feature/shared-keycard-module`
- Commit: `f6ed048`

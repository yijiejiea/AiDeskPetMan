# Asset Import Policy

This repository does not include Live2D Miku model files.

## Why

- Avoids copyright and redistribution issues.
- Keeps the repository small and fast to clone.

## Local Import Steps

1. Create a local model folder under `assets/skins/`, for example:
   `assets/skins/miku_local/`.
2. Put your model files in that folder (`*.model3.json`, `*.moc3`, textures, motions).
3. Keep these files local only. They are ignored by `.gitignore`.

## Voice and Model Assets

- Voice packs should be stored under `assets/voices/`.
- Local GGUF LLM files should be stored under `assets/models/`.
- Both directories are also local-only by default.

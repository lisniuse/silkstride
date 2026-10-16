# Contributor and agent conventions / 开发约定

## Commits / 提交

All commit messages **must use Conventional Commits (CC)**:
所有提交信息**必须遵循 Conventional Commits（CC）风格**：

```text
<type>[optional scope][!]: <description>
```

Use `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`,
`chore`, or `revert`. A scope is optional. Mark breaking changes with `!` or a
`BREAKING CHANGE:` footer. Keep the subject concise and describe the actual change.
类型使用上述标准类型；scope 可选；破坏性变更使用 `!` 或 `BREAKING CHANGE:`。

Examples / 示例：

- `feat(i18n): add Chinese and English language switching`
- `fix(feeding): target the visible filename before eating`
- `docs: update bilingual README`

## Terminal / 终端

- Use PowerShell 7 (`pwsh`) for terminal work / 使用 PowerShell 7。
- When RTK is installed, prefix shell commands with `rtk`. Wrap PowerShell cmdlets
  as `rtk pwsh -NoProfile -Command '...'` / 已配置 RTK 时，终端命令加 `rtk` 前缀。

## Project / 项目

- Native C++17, Win32, GDI+; build with `pwsh -NoProfile -File ./build.ps1`.
- Put user-facing translations in `src/i18n.h`; keep English and Chinese aligned.
  用户界面文案集中维护，中英翻译保持一致。
- Keep both language sections of `README.md` in sync / README 中英内容同步更新。
- Preserve existing pet memory and user preferences / 保留用户存档和偏好。
- Never use a user's existing Recycle Bin files for tests. The canary test may
  delete only the unique disposable file it created / 测试只能删除自己创建的临时文件。
- Run relevant diagnostics from `build/`. Non-destructive CI checks are motion,
  interaction, behavior, feeding, i18n, and startup tests. Startup tests must use
  isolated registry keys and never change actual sign-in startup entries.
  The input test moves the cursor;
  the canary test performs a real deletion of its own disposable file.

# 妙喵云境

这是 MiaoDesk 内置的分层动态壁纸资源包示例。

- `manifest.json`：标准 `.mdwall` 包元数据
- `scene.ini`：分层与动画参数
- `assets/`：背景、猫主体、尾巴、眨眼等独立素材

运行时由 `LayeredSceneRenderer` 加载。资源包缺失或校验失败时会自动回退到程序化 `PaintMiaoCloud`，不会导致壁纸空白。

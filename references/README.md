# 按需展开参考资料

原始文件保存在 `zip/`。此处只常驻 `manifest.json`；重复展开的参考工程已清理。

```powershell
python tools/prepare_references.py gcc
python tools/prepare_references.py rtdev hsi
python tools/prepare_references.py all
```

省略参数只更新清单。工具会去掉压缩包冗长根目录，写入 `references/gcc` 等短路径，并拒绝覆盖修改过的文件。完整映射见 `docs/SOURCES.md`。SDK 与 LVGL 源码常驻原目录，不需要重复展开。

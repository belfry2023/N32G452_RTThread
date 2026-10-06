# 工作区整理记录

日期：2026-10-04。

删除目标合计约 544.6 MiB；保留恢复归档并重新生成三个固件与验证产物后，工作区由约 771.5 MiB 降到 401.3 MiB，净减少约 370.2 MiB。构建产物继续变化时容量也会相应变化。

已移除 33 个目标，清单见 `CLEANUP_MANIFEST.json`：重复的长目录参考资料、`references/` 展开副本、重复 J-Link 补丁、旧 GP22 驱动、裸机入口/中断/链接模板、废弃 OpenOCD/J-Link 脚本、旧说明及历史构建目录。重复资料共 2119 个文件，删除前已逐字节比对原始压缩包。

保留原始 `zip/1.zip…11.zip` 与 CMSIS pack、`docs/` 原始 PDF、完整 Nations SDK、LVGL 及其已有 Git 元数据。未卸载本机工具、未改用户/系统 PATH、未改其他 STM/NXP/DSP 工程。

历史自有代码、配置和说明保存于 `zip/history/workspace-before-cleanup-20261004.zip`，归档已逐文件核对，SHA256 记录在清单中。恢复时将所需条目解压到单独目录比对，不建议覆盖当前工程。原始参考包按 `references/README.md` 按需展开到短路径。

当前只提供 `rtthread`、`rtthread-lvgl`、`rtthread-lvgl-release` 三个预设。新 `build/` 只含这些配置及本次验证产物，可重新生成。核查结果见 `VALIDATION.md`，官方 GCC 对照与链接说明见 `OFFICIAL_GCC_AUDIT.md`。

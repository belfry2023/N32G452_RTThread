"""Render build/pack-validation.json as GitHub Actions Job Summary markdown.

Standard library only, same as the rest of tools/. Prints to stdout so the
workflow can append it with `python3 tools/ci_summary.py >> "$GITHUB_STEP_SUMMARY"`.
Exits 0 even when the report is missing, so a failed build is not masked by a
second failure in the summary step.
"""
from pathlib import Path
import json
import sys

REPORT = Path(__file__).resolve().parents[1] / 'build/pack-validation.json'


def main():
    print('### CMSIS-Pack 校验')
    print()
    if not REPORT.is_file():
        print('未产出 `build/pack-validation.json` —— 构建或校验阶段已失败。')
        print()
        print('> 上一次的校验项与失败原因见本作业上方日志。')
        return
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    checks = report.get('checks', [])
    print(f"**{report.get('archive', '?')}** · {report.get('files', '?')} 个文件 · "
          f"sha256 `{report.get('sha256', '')[:16]}…`")
    print()
    for item in checks:
        print(f'- ✅ {item}')
    print()
    print(f'自动校验项：**{len(checks)} 项全部通过**')
    print()
    print(f"- 原生 Arm 编译：{report.get('native_arm_build', '见本作业日志')}")
    print(f"- 实板验证：**{report.get('hardware', 'NOT RUN')}**")
    print()
    print('> 本作业只证明交付物自洽（归档完整性、依赖闭包、示例源图、中断向量、'
          '器件包逐字节、隔离安装一致）。**不代表硬件验证。**')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print(f'CI summary failed: {error}', file=sys.stderr)
        sys.exit(0)

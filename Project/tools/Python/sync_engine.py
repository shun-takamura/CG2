"""ArcanaEngine と CG2_0_1 の間でエンジンのソースを同期する。

ArcanaEngine が本家。CG2_0_1（個人制作/ポートフォリオ）はエンジンを共有しているので、
エンジン側の修正を届けたいときや、CG2 側で直したものを吸い上げたいときに使う。

    py tools\\Python\\sync_engine.py to-cg2      # ArcanaEngine → CG2_0_1
    py tools\\Python\\sync_engine.py from-cg2    # CG2_0_1 → ArcanaEngine
    py tools\\Python\\sync_engine.py diff        # 差分の一覧だけ表示（コピーしない）

同期対象は ArcanaEngine.vcxproj が参照しているソース一式＋Common.props。
Sample/ や Game/ には触らない（それぞれのリポジトリ固有のため）。
"""
import io
import os
import re
import shutil
import sys

BS = chr(92)


def same_content(a, b):
    """改行コードと BOM の違いを無視して中身を比較する。

    .gitattributes の `* text=auto` により、コミット/チェックアウトを経たリポジトリと
    経ていないリポジトリで CRLF/LF が食い違う。それを差分として報告しないため。
    """
    def load(p):
        data = io.open(p, "rb").read()
        if data.startswith(b"\xef\xbb\xbf"):
            data = data[3:]
        return data.replace(b"\r\n", b"\n")
    return load(a) == load(b)

# このスクリプトは ArcanaEngine/Project/tools/Python/ に置かれている前提
ARCANA = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CG2 = os.path.abspath(os.path.join(ARCANA, "..", "..", "CG2_0_1", "Project"))


def norm(p):
    p = p.replace(BS, "/")
    while p.startswith("../"):
        p = p[3:]
    return os.path.normpath(p).replace(BS, "/")


def engine_files():
    """ArcanaEngine.vcxproj が参照するソース一式（Project/ からの相対）。"""
    vcx = os.path.join(ARCANA, "ArcanaEngine", "ArcanaEngine.vcxproj")
    text = io.open(vcx, encoding="utf-8-sig").read()
    files = set(norm(m.group(1))
                for m in re.finditer(r'Include="([^"]+\.(?:cpp|h|c))"', text))
    # externals は両者で同じものを使う前提なので同期対象から外す
    files = {f for f in files if not f.startswith("externals/")}
    files.add("Common.props")
    return sorted(files)


def run(mode):
    if mode == "to-cg2":
        src_root, dst_root, label = ARCANA, CG2, "ArcanaEngine → CG2_0_1"
    elif mode in ("from-cg2", "diff"):
        src_root, dst_root, label = CG2, ARCANA, "CG2_0_1 → ArcanaEngine"
    else:
        print(__doc__)
        return 1

    if not os.path.isdir(CG2):
        print("CG2_0_1 が見つかりません: %s" % CG2)
        print("（CG2 を別の場所に置いている場合はこのスクリプトの CG2 変数を直す）")
        return 1

    dry = (mode == "diff")
    print("%s%s" % (label, "  ※差分表示のみ" if dry else ""))
    print("  src: %s" % src_root)
    print("  dst: %s" % dst_root)
    print()

    changed, added, missing = [], [], []
    for rel in engine_files():
        s = os.path.join(src_root, rel.replace("/", BS))
        d = os.path.join(dst_root, rel.replace("/", BS))
        if not os.path.exists(s):
            missing.append(rel)
            continue
        if not os.path.exists(d):
            added.append(rel)
            if not dry:
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(s, d)
        elif not same_content(s, d):
            changed.append(rel)
            if not dry:
                shutil.copy2(s, d)

    verb = "差分あり" if dry else "更新"
    print("%s %d / 新規 %d / 欠損 %d" % (verb, len(changed), len(added), len(missing)))
    for x in changed:
        print("  %s %s" % (verb, x))
    for x in added:
        print("  新規 %s" % x)
    for x in missing:
        print("  欠損! %s" % x)

    if dry and (changed or added):
        print()
        print("反映するには to-cg2 / from-cg2 を指定して実行する。")
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1] if len(sys.argv) > 1 else "diff"))

"""Resources/Shaders/**.hlsl を DXC で事前コンパイルし Resources/CompiledShaders/ へ出力する。

エンジンは Debug では .hlsl を実行時コンパイルするが、Release / Development では
ここで作った .cso を読む（DirectXCore::LoadShaderBlob）。狙いは2つ。

  - 起動時のシェーダコンパイル待ちをなくす
  - 配布物に .hlsl を含めなくてよくする

プロファイルはファイル名から決まる。
    Foo.VS.hlsl -> vs_6_0    Foo.PS.hlsl -> ps_6_0    Foo.CS.hlsl -> cs_6_0
.hlsli はインクルード専用なので単体ではコンパイルしない。

    py tools\\Python\\compile_shaders.py           # 差分のみ
    py tools\\Python\\compile_shaders.py --force   # 全部コンパイルし直す
    py tools\\Python\\compile_shaders.py --clean   # 出力を消す
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

SHADER_DIR = Path("Resources/Shaders")
OUTPUT_DIR = Path("Resources/CompiledShaders")
INCLUDE_DIR = Path("Resources/Shaders/hlsli")

# 拡張子2段目 → シェーダプロファイル
PROFILES = {
    "vs": "vs_6_0",
    "ps": "ps_6_0",
    "cs": "cs_6_0",
    "gs": "gs_6_0",
    "hs": "hs_6_0",
    "ds": "ds_6_0",
}

# 実行時コンパイル（DirectXCore::CompileShader）と挙動を揃えるための必須オプション。
#   -Zpr : 行優先のメモリレイアウト。ここを外すと行列が転置されて描画が壊れる
# 実行時は -Od だが、事前コンパイルでは最適化を効かせる。
BASE_ARGS = ["-E", "main", "-Zpr", "-O3"]


def find_dxc():
    """Windows SDK 同梱の dxc.exe を探す。"""
    exe = shutil.which("dxc")
    if exe:
        return exe
    roots = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Windows Kits" / "10" / "bin",
    ]
    found = []
    for root in roots:
        if not root.is_dir():
            continue
        for ver in root.iterdir():
            cand = ver / "x64" / "dxc.exe"
            if cand.is_file():
                found.append(cand)
    if not found:
        return None
    # SDK バージョンが新しいものを優先
    found.sort(key=lambda p: p.parent.parent.name)
    return str(found[-1])


def profile_for(path: Path):
    """Foo.VS.hlsl → vs_6_0。判別できなければ None。"""
    stem = path.stem              # "Foo.VS"
    if "." not in stem:
        return None
    return PROFILES.get(stem.rsplit(".", 1)[1].lower())


def newest_include_mtime():
    """hlsli のどれかが更新されていたら全部コンパイルし直すための基準時刻。"""
    if not INCLUDE_DIR.is_dir():
        return 0.0
    times = [p.stat().st_mtime for p in INCLUDE_DIR.rglob("*.hlsli")]
    return max(times) if times else 0.0


def main(argv):
    force = "--force" in argv
    clean = "--clean" in argv

    if clean:
        if OUTPUT_DIR.is_dir():
            shutil.rmtree(OUTPUT_DIR)
            print("削除:", OUTPUT_DIR)
        else:
            print("出力は存在しません:", OUTPUT_DIR)
        return 0

    if not SHADER_DIR.is_dir():
        print("シェーダディレクトリが見つかりません: %s" % SHADER_DIR)
        print("（カレントディレクトリを Project/ にして実行すること）")
        return 1

    dxc = find_dxc()
    if not dxc:
        print("dxc.exe が見つかりません。Windows SDK が入っているか確認してください。")
        return 1

    include_mtime = newest_include_mtime()

    compiled = skipped = failed = 0
    unknown = []

    for src in sorted(SHADER_DIR.rglob("*.hlsl")):
        profile = profile_for(src)
        if profile is None:
            unknown.append(src)
            continue

        rel = src.relative_to(SHADER_DIR)
        dst = OUTPUT_DIR / rel.with_suffix(".cso")

        # 差分判定: 出力が .hlsl より新しく、かつ hlsli より新しければスキップ
        if not force and dst.is_file():
            dst_m = dst.stat().st_mtime
            if dst_m >= src.stat().st_mtime and dst_m >= include_mtime:
                skipped += 1
                continue

        dst.parent.mkdir(parents=True, exist_ok=True)
        cmd = [dxc, str(src), "-T", profile, *BASE_ARGS, "-Fo", str(dst)]
        if INCLUDE_DIR.is_dir():
            cmd += ["-I", str(INCLUDE_DIR)]

        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            failed += 1
            print("FAILED %s (%s)" % (rel, profile))
            for line in (proc.stdout + proc.stderr).splitlines():
                print("    " + line)
        else:
            compiled += 1
            print("  %-52s %s" % (str(rel), profile))

    print()
    print("コンパイル %d / スキップ %d / 失敗 %d" % (compiled, skipped, failed))
    if unknown:
        print("プロファイル不明のためスキップ（.VS/.PS/.CS が付いていない）:")
        for u in unknown:
            print("  ", u.relative_to(SHADER_DIR))

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

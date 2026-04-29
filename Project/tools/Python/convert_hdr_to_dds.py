from pathlib import Path
import sys
import subprocess

#cmftのパスオブジェクトを生成
cmft=Path("externals/cmft/cmftRelease.exe")

#Texconvのパスオブジェクトを生成
texconv=Path("externals/texconv/Texconv.exe")

processd=0
skipped=0

def process_hdr(hdr):
    """HDRファイルをDDSファイルに変換しBC6Hで圧縮"""

    global processd
    global skipped

    #.hdrがあれば出力
    print(f"[FOUND]:{hdr}")

    #出力先のパスオブジェクトを生成
    output_dds = Path(f"Resources/Cubemaps/{hdr.stem}.dds")

    #出力先に既にあればログを出力しスキップ
    if output_dds.exists():
        print(f"[SKIP]:{output_dds.name}")
        skipped +=1
        print()
        return#次のforループへ早期リターン
        
    print(f"[PROCESS]{hdr.name}")
    print(f"[1/2]Converting to Cubemap with Mipmaps...")

    # 中間DDSの出力先
    temp_output = Path(f"Temp/{hdr.stem}")

    # 中間DDSのフルパス (作成後の確認用)
    temp_dds = Path(f"Temp/{hdr.stem}.dds")

    result = subprocess.run([
    str(cmft),
    "--input",str(hdr),
    "--filter","none",
    "--generateMipChain","true",
    "--outputNum","1",
    "--output0",str(temp_output),
    "--output0params", "dds,rgba16f,cubemap",
    "--silent",
    ])

    if result.returncode!=0:
        print(f"[ERROR]cmft failed for {hdr.name}")
        return
        
    if not temp_dds.exists():
        print(f"[ERROR]cmft did not produce output for {hdr.name}")
        return
        
    print(f"[2/2]Compressing to BC6H...")

    result=subprocess.run([
    str(texconv),
    "-f","BC6H_UF16",
     "-o","Resources/Cubemaps",
    "-y",
    "-nologo",
    str(temp_dds),
    ])

    if result.returncode != 0:
        print(f"[ERROR]Texconv failed for {hdr.name}")
        return
        
    if temp_dds.exists():
        temp_dds.unlink()
        print(f"[INFO]delete {temp_dds}")

    processd+=1
    print(f"[DONE]{hdr.stem}.dds created")  

interctive_mode = True
if "--silent" in sys.argv:
    interctive_mode=False

print("=======================================")
print(" HDR to DDS Cubemap Conversion Started ")
print("=======================================")
print()

#cmftのパスオブジェクトを検索しなければエラーを出力し終了
if not cmft.exists():
    print("[ERROR] cmftRelease.exe is not found")
    
    #終了コード(1)失敗扱いで終了
    sys.exit(1)

#Texconvのパスオブジェクトを検索しなければエラーを出力し終了
if not texconv.exists():
    print("[ERROR] Texconv.exe is not found")
    
    #終了コード(1)失敗扱いで終了
    sys.exit(1)

#Assets
assets=Path("Assets")

#パスオブジェクトを検索しなければエラーを出力し終了
if not assets.exists():
    print("[ERROR] Assets is not found")
    
    #終了コード(1)失敗扱いで終了
    sys.exit(1)

#パスオブジェクトを生成
folder=Path("Resources/Cubemaps")

#パスオブジェクトを検索しなければログに出力してフォルダを生成
if not folder.exists():
    print(f"{folder} is not found")
    folder.mkdir(parents=True)
    print(f"[INFO] Created {folder} folder")

#パスオブジェクトを生成
folder=Path("Temp")

#パスオブジェクトを検索しなければログに出力してフォルダを生成
if not folder.exists():
    print(f"{folder} is not found")
    folder.mkdir(parents=True)
    print(f"[INFO] Created {folder} folder")

#Assetsフォルダ内の.hdrのパスオブジェクトを生成
for hdr in Path("Assets").glob("*.hdr"):
    process_hdr(hdr)

print()
print("=======================================")
print(" Conversion Completed")
print(f"Processed:{processd}")
print(f"Skipped:{skipped}")
print("=======================================")

if interctive_mode:
    input("Press Enter to exit...")
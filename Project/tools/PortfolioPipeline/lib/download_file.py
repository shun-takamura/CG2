"""Google Drive からファイルを 1 件取得する（PortfolioPipeline.ps1 から呼ばれる）。

使い方:
    python download_file.py                       # 認証だけ行う（初回のブラウザ認証用）
    python download_file.py <file_id> <保存先>    # ファイルを取得する

PowerShell との契約:
    - 成功時は終了コード 0。保存先には完成したファイルだけを残す。
    - 失敗時は 0 以外の終了コードと原因を stderr に出す。途中まで書いたファイルは消す。
    - Google スライド / ドキュメント / スプレッドシートは PDF にエクスポートする。

認証情報（このファイルと同じ lib/ に置く。どちらも Git 管理に含めない）:
    credentials.json : Google Cloud の OAuth クライアント（デスクトップアプリ）
    token.json       : 認証済みトークン（初回認証時に自動作成）
"""

import re
import sys
from pathlib import Path

from google.auth.exceptions import GoogleAuthError, RefreshError
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from googleapiclient.http import MediaIoBaseDownload

# 読み取り専用スコープだけを要求する
SCOPES = ["https://www.googleapis.com/auth/drive.readonly"]

# PDF にエクスポートする Google 形式
EXPORT_AS_PDF_MIME_TYPES = {
    "application/vnd.google-apps.presentation",
    "application/vnd.google-apps.document",
    "application/vnd.google-apps.spreadsheet",
}

# 終了コード
EXIT_OK = 0
EXIT_USAGE = 2
EXIT_AUTH_ERROR = 3
EXIT_DOWNLOAD_ERROR = 4

# ダウンロード進捗の表示用
PERCENT = 100


class GoogleDriveClient:
    """認証と Google Drive からのダウンロードをまとめる。"""

    def __init__(self):
        self.creds = self._get_credentials()
        self.service = build("drive", "v3", credentials=self.creds)

    def _get_credentials(self):
        """token.json を使い、無効なら更新、それも無理ならブラウザで認証する。

        Returns:
            有効な認証情報。
        """
        creds = None
        script_dir = Path(__file__).parent.resolve()
        credentials_path = script_dir / "credentials.json"
        token_path = script_dir / "token.json"

        if token_path.exists():
            creds = Credentials.from_authorized_user_file(str(token_path), SCOPES)

        if not creds or not creds.valid:
            refreshed = False
            if creds and creds.expired and creds.refresh_token:
                try:
                    creds.refresh(Request())
                    refreshed = True
                except RefreshError:
                    # 更新用トークンが失効している場合はブラウザ認証へ
                    print("トークンを更新できなかったため、ブラウザで再認証します。")
            if not refreshed:
                if not credentials_path.exists():
                    raise FileNotFoundError(
                        f"{credentials_path} がありません。Google Cloud で作成した OAuth クライアントを置いてください。"
                    )
                flow = InstalledAppFlow.from_client_secrets_file(str(credentials_path), SCOPES)
                creds = flow.run_local_server(port=0)
            token_path.write_text(creds.to_json(), encoding="utf-8")
        return creds

    def clean_filename(self, filename):
        """Windows で使えない文字を _ に置き換える。

        Args:
            filename: 元のファイル名。
        Returns:
            保存に使えるファイル名。
        """
        return re.sub(r'[\\/*?:"<>|]', "_", str(filename))

    def download_file_from_drive(self, file_id, output_dir):
        """ファイルを 1 件取得して output_dir に保存する。

        Args:
            file_id: Google Drive のファイル ID。
            output_dir: 保存先フォルダ。
        Returns:
            保存したファイルのパス。
        """
        metadata = self.service.files().get(
            fileId=file_id, fields="name, mimeType", supportsAllDrives=True
        ).execute()
        file_name = self.clean_filename(metadata.get("name", "file"))
        mime_type = metadata.get("mimeType")

        output_dir_path = Path(output_dir)
        if mime_type in EXPORT_AS_PDF_MIME_TYPES:
            output_path = output_dir_path / f"{file_name}.pdf"
            request = self.service.files().export_media(fileId=file_id, mimeType="application/pdf")
        elif mime_type and mime_type.startswith("application/vnd.google-apps."):
            raise ValueError(f"この形式は取得できません: {mime_type}")
        else:
            output_path = output_dir_path / file_name
            request = self.service.files().get_media(fileId=file_id, supportsAllDrives=True)

        output_dir_path.mkdir(parents=True, exist_ok=True)
        try:
            with open(output_path, "wb") as fh:
                downloader = MediaIoBaseDownload(fh, request)
                done = False
                while not done:
                    status, done = downloader.next_chunk()
                    if status:
                        print(f"ダウンロード進捗: {int(status.progress() * PERCENT)}%")
        except BaseException:
            # 途中で止まったファイルは成果物として残さない
            if output_path.exists():
                output_path.unlink()
            raise

        print(f"保存しました: {output_path}")
        return output_path


def main():
    if len(sys.argv) == 1:
        # 引数なし: 認証だけ確認する
        try:
            GoogleDriveClient()
        except (GoogleAuthError, FileNotFoundError, OSError) as e:
            print(f"認証に失敗しました: {e}", file=sys.stderr)
            return EXIT_AUTH_ERROR
        print("認証済みです。")
        return EXIT_OK

    if len(sys.argv) != 3:
        print("使い方: python download_file.py <file_id> <保存先>", file=sys.stderr)
        return EXIT_USAGE

    file_id = sys.argv[1]
    output_dir = sys.argv[2]

    try:
        client = GoogleDriveClient()
    except (GoogleAuthError, FileNotFoundError, OSError) as e:
        print(f"認証に失敗しました: {e}", file=sys.stderr)
        return EXIT_AUTH_ERROR

    try:
        client.download_file_from_drive(file_id, output_dir)
    except HttpError as e:
        print(f"ダウンロードに失敗しました (HTTP {e.resp.status}): {e}", file=sys.stderr)
        return EXIT_DOWNLOAD_ERROR
    except (ValueError, OSError) as e:
        print(f"ダウンロードに失敗しました: {e}", file=sys.stderr)
        return EXIT_DOWNLOAD_ERROR
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())

# mergiraf — 構文を理解する自動マージ

`git merge` は行単位でしか比較しないため、**二人が同じクラスに別々の関数を追加しただけ**でも
コンフリクト扱いになる。mergiraf は C++ を構文木として解析してからマージするので、
この種の「意味的には衝突していない」変更を自動で統合できる。

## セットアップ（クローン後に一度だけ）

`setup_mergiraf.bat` を**エクスプローラーでダブルクリック**するだけ。

やっていること:

1. `mergiraf_x86_64-pc-windows-gnu.zip` から `mergiraf.exe` を展開（初回のみ）
2. このリポジトリの `.git/config` に merge driver を登録

`.git/config` は Git の仕様上バージョン管理されないため、クローンしただけでは設定が入らない。
そのため各自が 1 回だけ実行する必要がある。

## 普段の使い方

**特別な操作は不要**。今までどおり `pull` / `merge` / `rebase` すれば自動的に使われる。

対象は `Project/DirectXGame/` 配下の `.cpp` / `.h`（`.gitattributes` で指定）。
`Project/externals/` は対象外。

## 効果の例

```cpp
// 共通の元
class Enemy {
    void Update() { pos += velocity; }
};
```

A さんが `TakeDamage()` を、B さんが `PlayDeathEffect()` を、それぞれクラス末尾に追加した場合:

| | 結果 |
|---|---|
| 素の Git | CONFLICT（`<<<<<<<` マーカーが発生） |
| mergiraf | 自動マージ成功（両方の関数が残る） |

## 注意点

- **セットアップしていない人がいても壊れない。** その人のマージが通常の Git マージに戻るだけ。
- **同じ関数の中身を二人が書き換えた**場合は、mergiraf でも当然コンフリクトする。
  勝手にどちらかを採用することはないので、そこは従来どおり人間が解決する。
- `#ifdef` などプリプロセッサで囲まれた領域は構文木が崩れるため、効かないことがある。

## バージョン

mergiraf 0.17.0 / https://mergiraf.org/

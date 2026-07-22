# PicoNeoCompat

[EN Version](https://github.com/kakik0u/PicoNeoCompat/blob/main/Readme-EN.md)

582.xx以降のNVIDIAドライバーで、Streaming Assistantを使用するPICO Neo2 / Neo2 Eye/ Neo3 でSteamVRを使えるようにする互換パッチ

> [!WARNING]
> 非公式のパッチです。PICO、Valve、NVIDIAとは無関係です。自分の責任で使ってください。

## 導入

### 0. 前提

- SteamVR がインストールされていること
- Streaming Assistantをインストール&初回セットアップ済みにしておいてください

### 1. ビルド済みパッケージをダウンロード

[Releases](https://github.com/kakik0u/PicoNeoCompat/releases/latest)から`PicoNeoCompat-win64.zip`をダウンロードし、展開します。

### 2. SteamVRを終了

SteamVRを完全に終了し、タスクトレイにも残っていないことを確認します。

### 3. `Install.bat`を実行

展開したフォルダー内の`Install.bat`をダブルクリックします。確認画面でキーを押すとWindowsの管理者確認が表示されるので、「はい」を選択してください。

インストーラーはPICOドライバーとSteamVRの標準インストールパスを参照します。違う場所にインストールしていた場合はディレクトリのパスを求められるので、

- `C:\Program Files (x86)\Streaming Assistant\driver`

のように入力してください。

### 3. SteamVRを起動
エラーなく起動できたら成功です。やったあ！

過去のクラッシュ履歴により、SteamVRが`pico`を自動ブロックする場合があります。(アドオンがブロックされました、みたいな表示が出ます)

その場合はSteamVRの「設定」→「スタートアップ/シャットダウン」→「アドオンの管理」で`pico`をブロック解除し、SteamVRを再起動してください。

### インストーラーが行う変更

1. PICOドライバーを`%LOCALAPPDATA%\PicoNeo2NvencCompat\driver`へコピー
2. コピー側の`VEncPlugin.dll`だけをパッチ（原本は変更しない）
3. H.264を使用するようコピー側の`RVRPlugin.ini`を設定
4. 互換DLLを配置
5. OpenVRのドライバー登録をコピー側へ切り替え

## 元に戻す

SteamVRを終了し、展開したフォルダーの`Uninstall.bat`をダブルクリックします。管理者確認で「はい」を選択すると、元のドライバー登録へ戻します。

コマンドラインから実行する場合：

```powershell
.\scripts\uninstall.ps1 -RemovePatchedDriver
```

## 確認済み環境

- PICO Neo 2 Eye
- GeForce RTX 3060 Ti（Ampere）
- NVIDIAドライバー 596.49
- SteamVR 2.16.7
- Windows 11 x64
- PICO Streaming Assistant付属の旧OpenVRドライバー

## 仕組み

```text
PICO VEncPlugin.dll
        │  nvEncCompat64.dll としてロード
        ▼
互換パッチ
  ├─ 旧プリセットGUID → P1–P7 + tuning info
  ├─ 廃止RCモード → CBR/VBR + multipass
  └─ NvEncGetEncodePresetConfig → ...ConfigEx
        │
        ▼
Windows\System32\nvEncodeAPI64.dll（本物のNVIDIAドライバー）
```

## ビルド

Visual Studio 2022またはBuild Tools 2022の「C++によるデスクトップ開発」をインストールされていることを確認し、PowerShellで次のコマンドを実行してください。

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1
```

`build\nvEncCompat64.dll`と`build\smoke_test.exe`が生成され、実際のNVIDIAドライバーをロードするスモークテストも自動実行されます。ビルド後は`Install.bat`、または詳細なパスを指定して`install.ps1`を利用できます。

```powershell
.\scripts\install.ps1 `
  -PicoDriverPath 'C:\Program Files (x86)\Streaming Assistant\driver' `
  -SteamVrPath 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR'
```

## ログ

互換DLLと同じフォルダーに`nvenc_compat.log`が生成されます。

## 注意事項

- 現時点で実機確認済みなのは上記の1環境です。追加で確認できたらIssueを立てて教えてください。
- SteamVRの更新・整合性チェックで互換パッチが削除される場合があります。その場合は再インストールしてください。

## 技術資料

- [解説記事](https://zenn.dev/kakikou/articles/66f8061fc269f3)
- [NVIDIA NVENC Preset Migration Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-preset-migration-guide/index.html)
- [NVIDIA NVENC deprecation notices](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/deprecation-notices/index.html)
- [NVIDIA NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)

## ライセンス

プロジェクト本体は[MIT License](LICENSE)です。NVENCヘッダーについては[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。

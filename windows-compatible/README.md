# ASPIRE for Windows

This folder contains the Windows-compatible version of the main ASPIRE assistant.
It uses Windows system sounds, Windows' default text-to-speech voice through
`pyttsx3`, and `os.startfile` for opening debug camera captures.

## Requirements

- Windows 10 or Windows 11
- Python 3.10 or newer
- A microphone and, for vision mode, a webcam
- Groq API key
- Optional NewsAPI and Google credentials for their corresponding features

## Setup (PowerShell)

```powershell
cd windows-compatible
py -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r requirements.txt
Copy-Item .env.example .env
```

Edit `.env` and provide your keys:

```dotenv
GROQ_API_KEY=your_groq_api_key
NEWS_API_KEY=your_news_api_key
```

For Gmail and Google Calendar integration, place your OAuth desktop-client file
at `credentials.json` in this folder. The first authorization creates
`token.json`. Both files are excluded by the repository's root `.gitignore`.

## Run

```powershell
python ASPIRE.py
```

Say "wake up" or press Enter to begin. Camera index `0` is used by default. If
Windows selects the wrong camera, change `cv2.VideoCapture(0)` in `ASPIRE.py`.

If PowerShell prevents virtual-environment activation, run this once for the
current terminal before activating it:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

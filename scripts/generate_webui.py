"""PlatformIO extra script: generate web_ui.h from index.html before build."""
Import("env")
import os

src = os.path.join("src", "web", "index.html")
dst = os.path.join("src", "web", "web_ui.h")

if os.path.exists(src):
    env.Execute(f"python3 scripts/html_to_progmem.py {src} {dst}")

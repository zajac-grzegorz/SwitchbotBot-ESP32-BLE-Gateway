Import("env")

# List installed packages
env.Execute("$PYTHONEXE ui/generate_settings_ui_single.py --schema ui/settings_ui.json --template ui/template.html --css ui/app.css --js ui/app.js --out embed/settings.html")
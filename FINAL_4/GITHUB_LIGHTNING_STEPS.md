# GitHub + Lightning (H200) Quick Steps

## Part A — Push this folder to GitHub (Windows PowerShell)

Run these commands in PowerShell from the parent folder:

```powershell
cd "e:\desk\daaass\finnnnnnnnnnn"
git init
git add FINAL_4
git commit -m "Add assignment code and Lightning H200 scripts"
```

Now create an empty GitHub repo in browser (no README), then run:

```powershell
git remote add origin https://github.com/<YOUR_USERNAME>/<YOUR_REPO>.git
git branch -M main
git push -u origin main
```

If prompted, sign in with your GitHub account.

---

## Part B — Run on Lightning AI (Terminal)

Open Lightning Studio with H200 GPU and run:

```bash
git clone https://github.com/<YOUR_USERNAME>/<YOUR_REPO>.git
cd <YOUR_REPO>/FINAL_4
chmod +x run_h200_skitter.sh
nohup ./run_h200_skitter.sh --timeout-hours 4 > launcher.log 2>&1 &
```

Watch progress:

```bash
tail -f launcher.log
```

Check final status:

```bash
LATEST=$(ls -dt artifacts/skitter_* | head -n1)
echo "$LATEST"
cat "$LATEST/STATUS.txt"
```

If SUCCESS, your exact output is here:

```bash
cat "$LATEST/results_as-skitter_gpu.txt" | head -80
```

Package files to download:

```bash
tar -czf skitter_h200_results.tar.gz "$LATEST" results_as-skitter_gpu.txt run_as-skitter_gpu.log
```

Download `skitter_h200_results.tar.gz` from Lightning file browser.

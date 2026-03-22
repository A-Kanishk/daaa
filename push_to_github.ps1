param(
    [Parameter(Mandatory=$true)]
    [string]$RepoUrl
)

Set-Location "e:\desk\daaass\finnnnnnnnnnn"

git branch -M main

$existing = git remote
if ($existing -match "origin") {
    git remote set-url origin $RepoUrl
} else {
    git remote add origin $RepoUrl
}

git push -u origin main

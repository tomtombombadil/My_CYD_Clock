# Putting this on GitHub

Everything is committed already. The folder you unzipped **is** the repository:
it has its full history in it, the version is 1.13.0, and the three flashable
files in `docs/firmware` are committed with it.

There are two commands left.

## 1. Push it

Open **PowerShell** in this folder. In File Explorer that is the address bar:
type `powershell` where the path is and press Enter.

```powershell
git remote add origin https://github.com/tomtombombadil/My_CYD_Clock.git
git push -u origin main
```

If GitHub asks who you are, sign in through the browser window it opens.

### If the push is refused

A message about *fetch first* or *non-fast-forward* means the repository on
GitHub is not empty, usually because a README or a licence was added when it was
created. Those files are already in what you are pushing, so there is nothing to
lose by replacing them:

```powershell
git push -u origin main --force
```

Only do that on a repository you have not put anything else in.

## 2. Switch the flashing page on

The web flasher is served by GitHub Pages out of the `docs` folder, and that has
to be turned on once by hand.

1. Go to <https://github.com/tomtombombadil/My_CYD_Clock>
2. **Settings**, then **Pages** down the left
3. Under **Build and deployment**, set **Source** to *Deploy from a branch*
4. Set the branch to **main** and the folder to **/docs**
5. **Save**

Give it a minute or two, then the flashing page is at:

**<https://tomtombombadil.github.io/My_CYD_Clock/>**

That address is already written into the README, so once Pages is on, the link
at the top of the repository works.

Anyone who opens it in Chrome or Edge, picks their board and plugs the clock in
over USB gets it flashed without installing anything.

## Afterwards

To push a change later, from the same PowerShell window in this folder:

```powershell
git add -A
git commit -m "what changed"
git push
```

If you have rebuilt the firmware in VS Code, the new `.bin` files under
`docs/firmware` are part of that commit, and the flashing page serves the new
version as soon as Pages catches up.

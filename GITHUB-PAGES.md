# Putting the flashing page on GitHub

This sets up a web page where anyone can install the firmware on their board by
clicking a button, with no VS Code and nothing to download. GitHub hosts it for
free.

Everything the page needs is already built and sitting in the `docs` folder.
This is about getting that folder onto GitHub and switching the hosting on.

---

## What is already done

Every time the firmware is built, a small step joins the four pieces a blank
chip needs into one file and puts it in `docs/firmware`. That is why there are
three files in there, one per board, each about 1.2 MB.

```
docs/
  index.html            the page people will visit
  manifest-cyd28.json   tells the page where each board's file is
  manifest-cyd32.json
  manifest-cyd35.json
  firmware/
    cyd28.bin           ready to flash, 2.8 inch
    cyd32.bin           ready to flash, 3.2 inch
    cyd35.bin           ready to flash, 3.5 and 4.0 inch
```

You do not have to maintain any of that. Build the firmware and it updates
itself, version number included.

---

## Trying it out first, without GitHub

Double clicking `docs/index.html` **does not work**, and the way it fails is
confusing: it gets as far as asking which COM port to use, then says
**Failed to download manifest**.

Talking to the USB port is allowed from a page opened off your hard drive, but
reading the firmware files sitting next to that page is not. That is a browser
security rule, and nothing to do with the board or the firmware. The page now
says so when you open it that way, rather than letting you walk into it.

To try it properly on your own computer, double click **`preview.bat`** in the
project folder. It serves the page the way a real web site would and opens it
for you. Close the window it leaves behind when you are done.

Everything works from there, including actually installing to a board. It is
the same thing GitHub will be doing, just on your own machine.

---

## 1. Make a GitHub account

If you do not have one, sign up at [github.com](https://github.com). It is free
and this needs nothing paid.

---

## 2. Make a repository

Click the **+** at the top right of GitHub, then **New repository**.

- **Name:** `my-cyd-clock` works well. It becomes part of the web address.
- **Visibility:** **Public**. GitHub only hosts pages from public repositories
  on a free account, and this will not work if it is private.
- Leave everything else alone and click **Create repository**.

---

## 3. Put the files in it

The simplest way, with nothing to install:

1. On your new empty repository page, click **uploading an existing file**.
2. Open your project folder on your computer.
3. Drag in everything in the project folder **except** `.pio` and
   `release-v1.8.0`. That is:

   - the folders `src`, `docs` and `licenses`
   - `platformio.ini`, `merge_firmware.py`, `preview.bat`, `.gitignore`
   - `README.md`, `CHANGELOG.md`, `FLASHING.md`, `GITHUB-PAGES.md`,
     `THIRD-PARTY-NOTICES.md`, `LICENSE`

4. Scroll down and click **Commit changes**.

Dragging a folder in takes everything inside it, which is what you want.

**Do not include the `.pio` folder.** It is hundreds of megabytes of things
GitHub does not need. If you cannot see it, good, Windows is hiding it and it
will not get picked up.

**`LICENSE` and `licenses` are two different things** and both matter. The
first is your licence for your own code. The second holds the licence texts
that have to travel with the firmware because of the web server library it is
built on. [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) explains why.

If you would rather not drag files around every time, GitHub Desktop does the
same job and is easier for repeat updates. That can wait until this is working.

---

## 4. Switch the page on

In your repository, click **Settings** along the top, then **Pages** down the
left hand side.

Under **Build and deployment**:

- **Source:** Deploy from a branch
- **Branch:** `main`, and for the folder pick **`/docs`**
- Click **Save**

The `/docs` part matters. It tells GitHub the web page lives in that folder
rather than at the top of the repository.

---

## 5. Wait, then visit it

Give it two or three minutes the first time. Refresh the Pages settings screen
and it will show your address, which looks like:

```
https://tomtombombadil.github.io/My_CYD_Clock/
```

Open it in Chrome or Edge, plug a board in, and click the button for your
board.

---

## 6. Two small edits

Two files still have a placeholder address in them, put there because there was
no way to know yours in advance.

In **`docs/index.html`**, at the bottom, and in **`README.md`**, near the top:

```
tomtombombadil
```

Change both to your own GitHub username. On GitHub you can click a file, click
the pencil icon, make the change and commit it, all in the browser.

While you are in the README, the line near the top about a photo is worth
acting on. Take a picture of your clock, drag it into the repository, and point
that line at it. A photo does more for a project page than any amount of
description.

---

## Updating it later

When you change the firmware:

1. Build each board in VS Code, the same as always. The files in `docs/firmware`
   update by themselves, and so does the version number the page shows.
2. Upload the changed files to GitHub, or press Push in GitHub Desktop.
3. Give it a minute or two. The page updates on its own.

Build **all three boards** before uploading, or the ones you skipped will still
be handing out the old version.

---

## Offering a download as well

The flashing page covers people with Chrome or Edge on a desktop. A release
covers everyone else, and gives each version a fixed address that does not
change under people when you update the page.

1. On your repository, click **Releases** down the right hand side, then
   **Create a new release**.
2. Click **Choose a tag**, type `v1.8.0`, and choose **Create new tag**.
3. Title it `v1.8.0`. In the description, say briefly what changed. The
   [CHANGELOG](CHANGELOG.md) has the wording already.
4. Drag the three `.bin` files from the `release-v1.8.0` folder into the
   attachments box, along with `SHA256SUMS.txt`.
5. Click **Publish release**.

Those files are named so it is obvious which board each one is for, because a
file called `cyd35.bin` means nothing to someone who has just landed on your
page.

They are written starting at address zero, so anyone can flash one with
esptool, the Espressif download tool, or anything similar, without needing to
know where the four pieces inside it go.

The `release-v1.8.0` folder itself is ignored by Git on purpose. Those files
are the same ones already in `docs/firmware`, just renamed, and there is no
sense storing 3.7 MB twice.

---

## Things worth knowing

**Only Chrome and Edge on a computer can do this.** The page talks to the board
over the USB cable using something Firefox and Safari have not implemented, and
phones cannot do at all. The page says so plainly to anyone who arrives in the
wrong browser.

**It has to be served, not opened as a file.** GitHub Pages does that for you.
Opening `index.html` off your hard drive fails partway through, which is what
the section near the top of this file and `preview.bat` are about.

**Nothing goes through a server.** The firmware file is downloaded to the
browser and written straight out of the USB port. Nobody's board talks to
GitHub or to you.

**Each version adds about 3.7 MB to the repository** and Git keeps every old
one. That is fine for years of a project this size, but it is why the `.pio`
folder is kept out: that would add hundreds of megabytes every time.

**People still need the USB driver** for the chip on their board, the same as
they would flashing any other way. The page tells them what to search for.

---

## If you want it to build itself

GitHub can compile the firmware and update the page on its own every time you
change the code, so you never have to remember to build all three boards. That
is a GitHub Actions workflow, and it is maybe thirty lines.

It is worth having once the manual version is working and you are happy with
it. Ask me and I will write it.

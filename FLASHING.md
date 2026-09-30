# Flashing My CYD Clock

This walks through putting the firmware on a board, start to finish, assuming
you have not used VS Code much. The only part that is new compared with a
single board version is step 4, where you tell it which board you have.

The three boards are:

| Pick this | Board | Screen |
|---|---|---|
| **cyd28** | 2.8 inch, ESP32-2432S028R | 240 x 320 |
| **cyd32** | 3.2 inch, ESP32-2432S032R | 240 x 320 |
| **cyd35** | 3.5 inch, ESP32-3248S035R | 320 x 480 |

The 4.0 inch board that uses the same panel as the 3.5 inch takes the
**cyd35** setting too.

If you are not sure which board you have, the model number is printed on the
back of the circuit board.

---

## 1. Get the files onto your computer

Download the zip and extract it somewhere you will find again, such as your
Documents folder. Open the folder you extracted and check that
`platformio.ini` and a folder called `src` are sitting right there.

Windows sometimes puts a folder inside another folder of the same name. If
that happened, the one you want is the inner one, the one with
`platformio.ini` directly inside it.

---

## 2. Open the project

Open VS Code. Choose **File**, then **Open Folder**, and pick the folder with
`platformio.ini` in it. Not the folder above it.

After a few seconds a row of small icons appears along the blue bar at the
very bottom of the window. That bar is where most of the work happens.

The very first time you open this project, PlatformIO downloads the tools and
libraries it needs. That is a few hundred megabytes and can take five to
fifteen minutes. There is activity in the panel at the bottom while it works.
Wait for it to go quiet before carrying on. It only happens once.

---

## 3. Plug the board in

Use a USB cable that carries data. Charge-only cables look identical and will
not work.

You do not need to pick a port. PlatformIO finds the board by itself.

---

## 4. Choose which board you have

This is the new part.

Look down the left hand edge of the VS Code window for a row of icons. One of
them is an **ant head** — that is PlatformIO. Click it.

A panel opens on the left. Find **Project Tasks** in it. You will see the
three board names listed:

```
  > cyd28
  > cyd32
  > cyd35
```

Click the arrow next to the one that matches your board. It opens up to show
a list including **Build** and **Upload**.

**That is the whole trick.** Clicking Upload *underneath a board's name* sends
the firmware to that board, built for that board. There is nothing to edit and
nothing to remember.

Ignore the buttons on the blue bar at the bottom for now. Those act on
whichever board is set as the default, which is the 2.8 inch one. Using the
sidebar means you never have to think about that.

---

## 5. Build it first

Under your board's name, click **Build**.

This compiles everything without touching the board, so it is a safe first
test. The panel at the bottom fills with lines of text and should end with a
green **SUCCESS** along with how much space the firmware uses.

If it stops with errors instead, copy the **first** error message and send it
to me. The first one is the real problem and the rest are usually knock-on
effects of it.

---

## 6. Upload it

Under the same board name, click **Upload**.

It builds again and then writes to the board. You will see a line counting up
in percent, then `Hard resetting via RTS pin`. The screen should come to life
within a second or two.

If it says it could not open the port, close anything else that might be
using it, such as a serial monitor left open in another window, and try again.

---

## 7. Watch it start up

Under the same board name there is also **Monitor**. Click that to see what
the clock is saying about itself. The speed is already set in the project, so
there is nothing to choose.

The first line tells you which board the firmware was built for and what size
screen it found. If that does not match the board in front of you, you picked
the wrong one in step 4.

---

## Switching to a different board later

Repeat step 4 with a different name, then Upload. Nothing else changes. You
can keep one project folder and flash all three kinds of board from it.

---

## If something does not look right

**The screen stays black, or shows nonsense, or the colours are clearly
wrong.** You picked the wrong board, or your board is a variant with a
different display controller. Check the model number on the back first. If it
is right, `platformio.ini` has a note in your board's section listing the
alternative to try.

**Colours look like a photographic negative**, for example a red clock coming
out cyan on a white background. Open the settings page, go to **Time Display**,
tick **Invert colours** and save. It changes the moment you save, with no
restart.

The 3.2 inch board needs this and already has it switched on. If you have a
panel that needs the opposite, untick it.

**Touch does nothing, or the factory reset button responds in the wrong
place.** The touch panel reports rough numbers rather than pixels and the
range varies between boards. Open **Monitor** and touch each corner of the
screen in turn. The clock prints where it thinks you touched and what the
panel actually reported. The notes at the bottom of `src/boards.h` explain
which numbers to change.

This only affects the factory reset button. Everywhere else a tap is just a
tap, anywhere on the screen.

**The build fails complaining about something it cannot find.** Under your
board's name in the sidebar there is also **Clean**. Click that, then Build
again. It throws away everything compiled earlier and starts fresh.

---

## Updating to a newer version later

Extract the new zip somewhere separate, then copy its `platformio.ini`,
`src` folder and `README.md` over your project folder, replacing what is
there. VS Code notices on its own. There is no list of files to keep in step
with: everything in `src` gets compiled automatically.

Leave the `.pio` folder alone. That is PlatformIO's own workspace, which is
why the next build is quick.

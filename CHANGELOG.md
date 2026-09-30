# Version history

Every change to My CYD Clock, newest first, with the reasoning behind it.
For what the project is and how to install it, see the [README](README.md).

---

## What changed in 1.13.4

**Stopping an alarm no longer leaves the colours flipped.** The alarm flashes
the screen by flipping the panel's own invert setting, a single command,
rather than repainting it. Tapping to stop the alarm switched straight back to
the clock, and the only code that put the colours back only ran while the
screen was still marked as the alarm screen, which by then it was not. A tap
that landed while the colours were flipped left them that way until a restart.
The colours are now put back whenever the alarm screen is left, however it is
left.

**Release files come out of every build again.** Each build now also leaves a
copy of its firmware in a folder named for the version, such as
`release-v1.13.4`, under a name that says which board it is for, with a list
of checksums alongside. Those are the files to attach to a GitHub release, and
GITHUB-PAGES.md now describes publishing one step by step. The instructions
had been describing that folder for some time, but nothing was making it.

---

## What changed in 1.13.3

This one came out of reading the source of the libraries the clock is built on,
looking for how they behave under the way this program uses them.

**Looking up server names the safe way.** Before every weather fetch the clock
turns a name like api.open-meteo.com into an address. The WiFi library's usual
way of doing that calls straight into the network stack from whichever task
asks. The network stack is only safe to call like that from its own task, and
in this build nothing stops a second task being in there at the same moment.
The time sync does its own lookups inside the network stack on its own
schedule, including in the same second or two after start up that the first
weather fetch happens. The same function also gives up after fifteen seconds
while leaving the network stack holding the address of an answer box on a
stack that no longer exists, so a late answer lands on whatever has moved in
since.

The weather task now asks the network stack's own task to do the lookup and
waits for it properly, connects to the address itself, and hands the open
connection to the web client. Handed an open connection, the web client uses
it as it is, so it never does a lookup of its own.

**The custom ringtone could erase itself.** The reply that fills in the
settings page had room for about 390 characters of custom ringtone. The JSON
library drops anything that does not fit without reporting it, so a longer
ringtone was simply missing from the page, and the next Save wrote the empty box
back over it. The reply is now sized to fit whatever ringtone is stored.

**The status reply was nearly full.** The reply behind the Status panel was
within a few dozen bytes of its limit, and the last large thing in it is the
restart summary, which 1.13.2 made longer. A long network name or place name
would have pushed it out, just when it is most wanted. It now has twice the
room, and both replies log a line if they ever run out again.

**A correction to the notes for 1.13.2** on reading the watchdog line: a task
that is stuck waiting is not running on either core, so the web server will not
necessarily be named. If core 0 says `IDLE0`, that alone means it was the web
server that ran out of time.

---

## What changed in 1.13.2

Everything in this release came out of a review of the whole program for things
that could crash it, hang it, or quietly do the wrong thing. None of it changes
what the clock looks like or how it is used.

**The settings page no longer changes things behind the clock's back.** The web
server answers each request on a task of its own, which can run on either core,
while the main loop is drawing on one core and the weather task is working on
the other. Pressing Save used to rewrite the settings, redraw the screen and
change the colour inversion right there, in the middle of whatever the other two
were doing. Two things go wrong with that. A text setting being replaced on one
core while another core is copying it hands the copier memory that has just been
freed, and the display library cannot cope with two tasks talking to the screen
at once. Either can crash the chip.

Now a request only works out what was asked for, puts it in a mailbox and
replies. The main loop picks it up on its next pass, a few milliseconds later,
and does the work itself. The same goes for the alarm Test buttons, Hear it,
restart, forget WiFi and factory reset. The settings themselves have a lock, and
anything that reads their text from another task takes a copy under it first.

**Changing the postcode during a lookup now works.** If a new postcode was saved
while the old one was still being looked up, the old lookup finished, saved the
old town's position under the new postcode, marked it as known, and cancelled
the request to look the new one up. Because the position is saved, that stuck
for good. The weather task now works from a copy of the settings taken at the
start of each fetch, and throws a result away if the postcode changed while it
was on its way.

**The watchdog now records both cores.** 1.13.1 recorded only what core 0 was
running when the watchdog fired. But the watchdog also times the web server
while it answers a request, and the web server can be on either core. If that
is what took too long, core 0 was running something innocent at the time, often
its own idle task, and the record would have pointed the wrong way. The restart
line now names both:

```
When the watchdog fired, core 0 was running "xyz" and core 1 was running "abc"
```

How to read it: the watchdog is watching only two things, core 0's idle task
and the web server while it answers a request. If core 0 names a real task,
that task hogged core 0 and kept the idle task off it. If core 0 says `IDLE0`,
the idle task was running fine, so it was the web server that ran out of time.
It may not be named on either core, because a task stuck waiting for something
is not running anywhere.

**A stuck tone, and two tunes at once.** The sound task read what it was meant
to be playing, then set the note. An alarm dismissed in between had its note
switched on after the stop, with nothing left to switch it off. Separately,
starting a new sound while another was playing replaced the notes while the
sound task was still reading them. The notes and the job are now read and
changed under one lock. The sound task also sleeps longer when nothing is
playing, instead of waking five hundred times a second for no reason.

**Ringtone lines that could crash or freeze the clock.** A `b#` read one past
the end of the table of notes. A huge octave number, which only a mangled
ringtone line would have, made the note calculation loop that many times:
`o=2000000000` would have kept the chip busy for half a minute, long enough for
the watchdog to restart it. Octaves are now held between 1 and 9, and a long
ringtone is trimmed when it is saved rather than silently failing to save.

**Less needless work.** The clock face used to work its whole layout out again
about a hundred times a second, measuring every numeral and formatting the date
into fresh strings each time, when the picture changes at most once a second.
It now only does that when something on it can actually change. The activity
log keeps its lines in fixed slots instead of strings, so logging no longer
chips away at free memory all day.

**The weather screen could miss fresh readings.** The count the screen watches
to know when to repaint went up before the new readings were in place, so the
screen could repaint from the old ones and then have no reason to repaint
again.

---

## What changed in 1.13.1

**Version numbers.** The middle number was being bumped for everything,
including tweaks. From here it is the third number for fixes and small changes,
the middle one only for something genuinely new, and the first if saved settings
ever stop being readable.

**The watchdog now names the culprit.** The 2.8 inch board was restarting a few
seconds after start up, and the kept log said why in general terms: *task
watchdog, something stopped responding*. That means the idle task on core 0 was
kept off the processor for five seconds by something that would not let go. It
also ruled a few things out: free memory never dropped below 153 KB, so nothing
had run out of room, and the run died eight seconds in, which is five seconds
after the weather fetch finished.

What the message did not say is *which* task. The watchdog knows, because it
looks before it panics, so the clock now asks it. A handler runs inside the
watchdog's own interrupt, reads the name of whatever is holding core 0, and
copies it into the memory that survives a restart. The next time this happens
the restart line reads:

```
Restart number 1, cause: task watchdog, something stopped responding.
The previous run lasted 8 seconds and its lowest free memory was 153488 bytes.
The task holding core 0 when the watchdog fired was "xyz"
```

Nothing has been changed on a hunch to try to fix the hang itself. Two guesses
have already been made in this project and both were wrong, so this one gets
evidence first.

---

## What changed in 1.13.0

**The Turkish March, properly this time.** Not transcribed here but pasted in as
a ringtone line that has been around for years and played by a great many
phones, which is exactly what the format is for. The hand written attempt in
1.11.0 was not recognisable.

**Westminster chimes removed.** Neither was that.

**The settings page explains the format.** Under **Your own ringtone** there is
now a worked example, a note that raising the `o` value by one lifts a tune an
octave for a small speaker, and a link to
[rtttl-hub.io](https://rtttl-hub.io) for finding more.

**Longer ringtone lines.** The buffer was 600 characters and the Mozart line is
478, which is closer than it ought to be. It now takes 1200, and the box on the
settings page stops you at the same figure rather than letting a longer one be
quietly cut short.

---

## What changed in 1.12.0

**The alarm no longer fights the speaker.** The sound dropping out in time with
the flashing screen was the screen's fault. The alarm screen was being repainted
from scratch two and a half times a second, which is a hundred and fifty
kilobytes down the wire and a lump of current drawn with it, right next to a
small amplifier. The screen is now painted once and flashed by flipping the
panel's own invert bit, which is a single command and no traffic at all.

**The classic alarm clock sound, and it is the default.** 125 milliseconds of
tone, 125 of silence, 125 of tone, 625 of silence, at 2 kHz, with a 4 kHz
version beside it. Given a clock drawn to look like a seven segment display, it
was the obvious thing to wake up to.

**Sounds are ringtone lines now, and you can paste your own.** All of them, in
the old Nokia format that rtttl.cpp reads:

```
Classic2k:d=16,o=7,b=120:c,p,c,p,p,p,p,p
```

The tunes used to be tables of notes written out here by hand, and two of the
pieces that came out of that were wrong enough not to be recognisable. Ringtone
lines fix that at the root: thousands of them have already been written out and
checked by other people, they cost almost nothing to store, and **Your own
ringtone** on the settings page takes one straight from you.

The Entertainer and the Minuet in G are gone; neither was right. What ships is
plain patterns, which nobody owns, and music old enough to be out of copyright.
Ringtones of songs and film themes are somebody's property and are not included,
though nothing stops you pasting one into your own clock.

**Hear it says what it did.** It was reported not working again, and guessing at
it twice was enough. It now writes a line to the activity log when the request
arrives and another when the sound starts, saying which tune, how many notes and
how long. If it stays quiet the log says where it stopped. The preview is also
stopped by the main loop now as well as by the sound task, so the one job that
has to stop itself no longer depends on two cores agreeing about it.

---

## What changed in 1.11.0

**The music is an octave lower and there is a lot more of it.** Two octaves
above written pitch was where the speaker is loudest, but it was also piercing.
One octave up is the compromise: still clear of the range where a speaker this
size moves almost no air, without being shrill.

**The Entertainer is gone.** Neither phrasing of it was right.

**Three pieces now, and each plays a real stretch of the music** rather than a
phrase that comes round every few seconds:

| Sound | Length before it repeats |
|---|---|
| Für Elise, Beethoven | 19 seconds — the whole opening section |
| Minuet in G, Bach | 20 seconds — sixteen bars, both halves of the first section |
| Turkish March, Mozart | 6 seconds — the opening |

There is no real limit here. A note costs four bytes, so even a piece running
several minutes would be a few kilobytes of a flash chip with megabytes spare.
Length is a matter of taste, not of room.

**The tunes are generated now.** `src/tunes.h` is written by a small script that
also renders the same tables to audio files, so a change can be listened to on a
computer before it is ever flashed, and what is auditioned is provably what the
firmware plays rather than a separate transcription of it that might drift.

---

## What changed in 1.10.0

**The sounds were too fast, and one of them never played at all.**

**Hear it now works.** It was meant to play on the clock, not through the
browser, and it played nothing. The preview was the only sound that stops
itself after one pass, and that turned out to be the whole problem. It set its
start time and its "go" flag as two separate writes, while the sound is played
by a task on the *other* core. That task could see the flag go up while the
start time was still the old one, conclude the sound had finished long ago, and
switch off before a single note came out. The alarms were unaffected because an
alarm never stops itself, which is exactly why the Test buttons worked. The two
tasks now hand the job over under a lock, so the reader never sees half of one
job and half of another.

**Everything is much slower.** The beeps are 500 milliseconds rather than 225,
with a 250 millisecond gap. The chime's notes are 900 milliseconds rather than
300. The trill flips between its two notes every 90 milliseconds rather than
every 32: at the old rate the ear stops hearing two notes and starts hearing one
rough one, which is why it did not warble.

**Two pieces of music.** The clock has one speaker on one pin, so it has one
voice and that voice is a square wave. That rules out anything with chords, but
it suits a melody played one note at a time, so there is now:

- **Für Elise**, Beethoven — the opening
- **The Entertainer**, Joplin — the opening bars, ragtime rhythm kept

Both are moved two octaves above where they are written for a piano. A speaker
this size moves almost no air below about a kilohertz, so at their written pitch
they would be nearly inaudible; moved up, they sit where the speaker is at its
best.

**Sounds are tables now, not code.** All five live in `src/tunes.h` as plain
lists of notes and durations, and one routine plays any of them. Adding another
tune is adding a table. It also means the sounds can be rendered to audio on a
computer straight from those tables, so a change can be listened to before it is
ever flashed.

---

## What changed in 1.9.0

**The alarm sounds better, and you can choose what it sounds like.**

The old tone was a bare square wave switched hard on and hard off, three times
every two seconds. Most of what made it unpleasant was not the note but the
switching: a speaker handed a step change reports it as a click, so every beep
arrived wrapped in two of them. Every note now fades in and out over about
twelve milliseconds, which is long enough to lose the click and short enough
that the beep still sounds like a beep.

There are three sounds to pick from on the settings page:

| Sound | What it does |
|---|---|
| **Beep** | Three long beeps, the classic alarm clock |
| **Trill** | The same three bursts, each fluttering between two notes about eighteen times a second |
| **Chime** | Two soft falling notes with a long fade, for waking up kindly |

**The beeps are longer and closer together,** as asked: each one runs 225
milliseconds rather than 150, and the gap between them is 100 milliseconds
rather than 150, so they come half again as quickly.

**There is a volume setting,** and a **Hear it** button beside it that plays the
current choice straight away without saving anything or setting an alarm off.

Volume is done by changing how much of each cycle the pin spends driving the
speaker. Two corrections are folded into the slider. The speaker only really
passes the fundamental of the square wave, whose size follows a sine of the
pulse width rather than the width itself, so the width is worked back through an
arc sine. And the ear hears loudness on a squashed scale, so the setting is
squared first. Without those two, nothing at all happens over the first three
quarters of the slider and everything happens in the last few percent.

Note that **100% is where the clock already was**: half of each cycle is as hard
as a square wave can push, so the setting works downwards from there. If 100% is
not loud enough, the speaker is the thing to change, not the firmware.

**The sound runs on a task of its own.** It has to. The main loop comes round
about every fifteen milliseconds, which is longer than the fade at each end of a
note and half as often as the trill changes note. Run from there, the fades
would not have happened at all and the trill would have stumbled. The speaker
now has a small task pinned to the other core that wakes every two milliseconds,
reads what it is supposed to be playing, sets one register and goes back to
sleep.

---

## What changed in 1.8.0

**Different settings out of the box.** Nothing you cannot change on the
settings page, but a clock fresh from the flashing page now starts with the
colon not blinking, the date shown, AM and PM shown, seconds off, a 2 pixel
line between segments, brightness at 150 and night brightness at 10.

**A proper screen after it joins your network.** It used to squeeze four lines
into a shape meant for a short message, and the last of them,
*Touch and hold the screen to see this again*, ran off both edges at once on
the wider board. It is now its own screen:

```
        Configure your clock at:
          http://192.168.4.148
                    or
      http://my_cyd_clock_6c.local
         Long press the screen
         to see this info again.
```

**Nothing can run off the edges any more.** That overflow happened because the
sizes were chosen once, by eye, for the narrow board. The wider board uses a
larger plain typeface, so the same sentence needs half as much room again and
there was none.

Every line on these screens now walks down a ladder of typefaces until it finds
the largest one that actually fits the width, measured rather than assumed. The
block is then centred by its measured height. A longer address or a reworded
message cannot break the layout.

**The status page reads better.** The heading and the address are white rather
than the clock colour, and the address is larger, spelled out as
*Browse to http://... to configure your clock*.

The **IP** row is gone from the list of details. The address it held is now
printed in full, larger, a few lines below it, so the row was saying the same
thing twice in a smaller typeface. Dropping it is what freed the room for the
larger address, and on the wider board it let the whole list of details move up
to the larger typeface as well.

Both lines of that address are now the same size and the same white, so they
read as one sentence rather than a heading with a footnote under it.

**The erase confirmation cannot overflow either.** Checking the rendered
screens against the real typeface tables turned up one last line the ladder had
not been applied to: *Touching anywhere else keeps your settings* measured 494
pixels against the 480 the wider board has. That screen now walks the same
ladder, its paragraph is wrapped to fit the larger typeface rather than fall
back to a smaller one, and the line itself is shorter: *Touch elsewhere to keep
your settings*.

**Screenshots.** `docs/screenshots/` holds a picture of each screen, ready to
drop into the README.

---

## What changed in 1.7.3

**Changing the ZIP code now actually changes the weather.** It was possible to
save a new postcode and have the clock keep showing the old town's weather for
good.

A postcode is not a position. The clock looks the position up once, from the
postcode, and keeps it so it does not have to look it up again. When the
postcode changed, that saved position was supposed to be thrown away with it.
The instruction to do that ran, but it ran *after* the settings had already
been written out, so what ended up stored was the new postcode sitting next to
the old position, still marked as known.

From then on nothing ever looked the new postcode up, on that run or any run
afterwards, because as far as the clock could tell it already knew where it
was. Only in memory did it look correct, so it could appear to work until the
next restart and then go wrong permanently.

The position is now cleared before the save rather than after, and the readings
for the old place are thrown away at the same time, so the screens say they are
fetching rather than showing another town's weather.

**Two jobs could tread on each other while writing settings.** The web page
writes settings when you press Save. The weather task writes the position it
has just looked up. They run at the same time, and both were opening the same
storage, which can only be open once. One could close it out from under the
other part way through, and whatever that one was writing would quietly go
nowhere.

They now take turns. The second one to arrive waits rather than trampling the
first. This was not necessarily what you hit, but it is the kind of fault that
shows up as a setting that sometimes does not stick, which is miserable to
chase later.

**Each weather fetch logs the position it is using**, so it is obvious from the
log whether a stale position is in play.

**The flashing page explains the Erase option** rather than just telling you to
tick it.

---

## What changed in 1.7.2

**Every build now produces one ready to flash file per board.** A normal build
leaves four separate pieces that have to be written to four different addresses
on the chip. PlatformIO knows those addresses, which is why flashing from VS
Code works, but anyone doing it another way would have to know all four.

Those four are now joined into a single file that gets written starting at
address zero, and it lands in `docs/firmware`. This happens on its own after
every build. The step that does it is `merge_firmware.py`, and it also writes
the small description file the web flashing page reads, so the version shown
there can never drift out of step with the firmware.

**There is a web flashing page.** It lives in `docs`, ready to be hosted by
GitHub for free. Someone opens it in Chrome or Edge, plugs in a board, clicks
the button for their board, and the firmware is written over the USB cable.
No VS Code, nothing to download, nothing to know.

Each board gets its own button. All three are plain ESP32 chips, so a page
cannot work out which one is plugged in, and asking is more honest than
guessing.

**[GITHUB-PAGES.md](GITHUB-PAGES.md)** walks through putting it up.

**The top right of the Current Weather screen is empty now.** It said how long
ago the reading was taken, which for a screen that fetches the weather when you
tap it almost always read "just now". When there is no reading at all the
reason still appears there, and the status screen shows the age either way.

---

## What changed in 1.7.1

**Invert colours is a setting now.** It is under **Time Display** on the
settings page. Some panels treat a colour value as its opposite, so asking for
red numbers on a black background gets you cyan on white. Ticking the box turns
that round, and it takes effect the moment you save rather than needing a
restart.

The 3.2 inch board needs this, so its build starts out with the box already
ticked and there is nothing to do. The other two start out unticked. Whichever
board you have, if the screen looks like a photographic negative the fix is one
tick box rather than editing a build file.

This is a setting rather than a build flag on purpose: panels vary between
production runs of the same board, so a fixed value per board would only ever
be a guess about the particular panel in front of you.

**The Show Password box on the setup page works the right way round.** The
password is shown as you type it, which is deliberate, but the tick box next to
it was showing unticked.

The cause: that page's own handler decides which way to flip by looking at what
the box is showing at that moment. Showing the password without also ticking
the box left the two disagreeing, so the first click hid the password while
ticking the box, and everything after that was backwards. Both are now set
together.

---

## What changed in 1.7.0

**Three boards from one project.** The differences between them are small but
they are the kind that stop a board working at all rather than looking
slightly wrong:

- The backlight is on pin 21 on the 2.8 inch board and pin 27 on the other
  two. Get this wrong and the screen stays dark.
- The display controller differs. The 3.5 inch uses an ST7796 rather than the
  ST7789, and it is a bigger screen: 480 across by 320 down instead of 320 by
  240.
- **The touch panel is wired differently.** On the 2.8 inch board it has four
  wires of its own. On the 3.2 and 3.5 inch boards it shares the display's
  wires. Two things cannot drive the same wires independently, so on those
  boards the display library has to read the touch panel rather than the
  separate library doing it.

Everything that differs now lives in one file, `src/boards.h`, and the rest of
the program never asks which board it is on. The touch panel is read through
one set of functions in `src/touch.cpp` that cover both wiring styles.

**Every screen sizes itself now.** This was the larger job. The clock face
always worked out its own size, which is why it adapts when you switch seconds
or the date on. Everything else had pixel positions typed in for a 320 by 240
screen: the big temperature at x 16, the weather picture at 252, the status
rows starting at y 30 and stepping 17 pixels. On the 3.5 inch board all of
that would have huddled in the top left corner with two thirds of the screen
empty.

Those are all gone. The screens now work in fractions of whatever screen they
find themselves on, and the typefaces step up a size on the larger board. The
status page works out its own spacing from how much room is actually left
above the reset button, so it cannot overlap it.

The buttons are worked out the same way, by a function that both the drawing
and the touch test call. One function used by both cannot disagree with
itself, which the old pair of typed-in coordinate sets could have.

**The log says which board it was built for**, and what size screen it
actually found, on the first two lines. If those disagree with the board in
front of you, you picked the wrong one.

### What I could not test

I only have the 2.8 inch board's behaviour to go on. The 2.8 inch build is
unchanged in every way that matters and the layout at 320 by 240 comes out
pixel for pixel where it did before.

For the other two, the pin numbers come from the manufacturer's own board
definitions rather than from memory, so those I am confident about. The part I
am not sure of is the **touch mapping on the 3.2 and 3.5 inch boards**: which
way round the panel reports its two directions, and where its usable range
ends. I have put in the most likely values and made them easy to change.

This only affects the factory reset button, the one place in the firmware that
cares where you touched rather than just that you did. Everywhere else a tap
is a tap, anywhere on the screen. If that button misbehaves, open the serial
monitor and touch each corner: the clock prints where it thinks you touched
and what the panel actually reported. The notes at the bottom of
`src/boards.h` say which numbers to change.

---

## What changed in 1.6.0

**The background refresh setting is gone.** The weather is now fetched only
when there is a reason: once when the clock starts so the screens are not
empty, whenever you tap through to a weather screen, and twice a day if
**Dim after sunset** is on, because that needs sunrise and sunset times. There
is no timer and nothing to configure.

**The clock's name includes part of its hardware address.** It is
`my_cyd_clock_` followed by the last two characters of the board's own address,
for example `my_cyd_clock_6c`. That is fixed for a given board, so two of these
on one network each keep their own name. It appears on the touch and hold
screen, on the start up screen next to the address, and in the status box.

One thing to know: underscores are unusual in network names and a few older
devices will not look one up. The address always works. If you hit that, change
`CLOCK_HOSTNAME_PREFIX` in `src/config.h` to use hyphens instead.

**The setup page opens straight onto the WiFi page.** The delay after tapping
the button was the library scanning for networks with everything else stopped,
which is also why the button greyed out and came back. The scan cannot be made
to happen in the background, because the settings that would allow it are not
reachable from outside the library. So the menu page is skipped entirely and
you land on the WiFi page directly. There is one wait, while a page is visibly
loading, instead of a quick page followed by a stall.

**The password shows as you type it.** This is a home clock, not a bank.

**The settings page sections fold up.** Everything except Status is now a
section you tap to open, closed to begin with. Save and Reset sit together at
the top and stay there as you scroll. The old section at the bottom is gone,
and Restart the clock has moved into the Status box where the rest of the
device state lives.

Renamed: WIFI to WiFi, Time to Network Time, Display to Time Display.

**Weather screens renamed,** on the settings page and on the clock itself:
Current Weather, 4 Hour Forecast, Daily Forecast. The daily one still counts
the days you chose, so it reads 7 Day Forecast and so on.

**The hourly screen starts at the next hour** and shows four of them across a
single row, larger than before. It used to start with the hour you were already
in, which the current weather screen already covers.

**Forecast length is a slider,** 3 to 10 days, since 10 is all that fits on the
screen anyway.

**Reset to defaults is on the settings page too,** next to Save. It asks first,
then clears every setting and both copies of the WiFi network, the same as the
button on the clock's own screen.

---

## What changed in 1.5.0

**Tapping refetches straight away, even after a failure.** This was a real bug.
Tapping through to a weather screen asks for a fresh reading unless there is
one less than half a minute old. The check was asking when the weather was last
*tried* rather than when it was last *fetched*, and those are not the same. A
failed attempt therefore locked out tapping for the next thirty seconds, which
is exactly the moment you most want it to try again. It now asks the right
question, and a tap also jumps ahead of whatever the background retry was
waiting for.

The backing off from 1.4.5 still exists, but it only governs the clock retrying
on its own in the background. It never delays you.

**The clock only fetches the weather when it is going to be used.** That was
always the intent and it now holds properly: once at start up so the screens
have something on them, and again whenever you tap through to a weather screen.
Nothing else.

**So why is there a background refresh setting?** There should not have been
one, and in 1.6.0 there is not. It only ever existed because dimming after
sunset needs sunrise and sunset times, which arrive with the forecast. That is
now handled on its own, twice a day, and the setting has been removed.

**Nothing was remembering your ZIP code.** The factory reset clears every
stored setting, and it always did. 16146 was the built in default, carried
across from the file you first sent me, so that is what the clock fell back to
after a reset. It looked like it had remembered when in fact it had forgotten
and landed on the same value.

The default is now **15213**, Oakland in Pittsburgh.

---

## What changed in 1.4.5

**The radio no longer dozes.** Left to itself the chip sleeps between the
access point's beacons to save power, and anything that arrives while it is
asleep waits for the next wake up. On a mains powered clock that power saving
is worth nothing, and it is the usual reason a request that should take a
fraction of a second times out instead. The radio is now kept awake.

This is the most likely cause of the two failed weather fetches at start up,
though it is not proven. What the log did show was a **read** timeout, which
means the clock reached the weather server and connected to it, then waited
without a reply. The server itself was ruled out by measurement: it answers in
between a tenth and half a second, consistently.

**A failed fetch is retried sooner.** It used to wait a full minute after every
failure, so two slips left the screen saying there was no weather for over two
minutes. It now waits 10 seconds, then 20, then 40, then settles at a minute.
This is the clock retrying by itself in the background; from 1.5.0 a tap never
waits for it.

**Every stage of a fetch is timed in the log.** A failure could previously have
been slow name lookup, a slow connection, or a slow reply, and the log did not
say which. It now looks like this:

```
Forecast: found api.open-meteo.com at 172.66.0.208 in 34 ms
Forecast: received 2031 bytes in 412 ms
```

If it fails again, those two lines will say exactly which stage is at fault.

### What was ruled out

The name service added in 1.4.4 was the obvious suspect, being the new thing on
the network path. Your log rules it out: it was running during the fetch that
**succeeded**, so it cannot be what stopped the two that failed. It has been
left alone.

---

## What changed in 1.4.4

**The settings page works after first setup.** The clock would answer a ping
but nothing on port 80. The setup page runs its own web server on that port,
and the chip does not reliably hand the port back when that server is shut
down. This is a known weakness, and the library says so in a comment in its own
source. Our settings page then had nowhere to listen, so the clock looked alive
on the network and answered nothing.

The clock now restarts once, straight after your network has been saved. The
next start up connects to your network directly, the setup page never runs at
all, and port 80 is free. The screen says **Connected to (your network),
restarting to finish setup** so it is clear what is happening.

This only happens the once, on the run where you set the clock up.

**The clock has a name on the network now.** It is **cyd-clock**, so it appears
under that name in your router's list of devices, and most phones and computers
can reach the settings page at:

```
http://cyd-clock.local
```

The address still works too, and is the thing to fall back on: name lookup of
this sort is handled by the device asking, and while phones, Macs and recent
Windows all do it, some networks and older setups do not.

The name is shown on the touch and hold screen and in the status box on the
settings page. To change it, edit `CLOCK_HOSTNAME` at the top of
`src/config.h`, which is worth doing if you build more than one of these.

---

## What changed in 1.4.3

**Start up clears the chip's copy of the network again.** Version 1.4.2 took
that out, which was wrong. It is there on purpose: the chip remembers the last
network it used quite separately from our settings, and left in place it would
quietly reconnect on its own so the setup page would never appear. That makes
the first run experience impossible to test, because a factory reset would look
like it had worked and then the clock would silently rejoin the old network.

It only happens when there is no network in our own settings, which means
either the clock has never been set up or it has just been reset.

This was safe to put back because the reason it caused trouble is gone. In
1.4.1 the clock could connect, crash before writing the network down, and come
back with empty settings through no fault of the user, and this line then threw
away the last copy of the network. Now the network is written down the instant
the connection succeeds, so empty settings only ever mean what they are
supposed to mean.

**Worth knowing about the trade off.** If the clock ever does lose its own
settings while the chip still knows the network, it will ask you to set it up
again rather than repairing itself. That is the deliberate choice: being able
to trust a factory reset is worth more than automatic recovery from something
that should not happen.

---

## What changed in 1.4.2

**The setup loop is fixed.** Entering your network and pressing the button
connected successfully, then the clock rebooted and asked again, forever. Five
things had to line up to produce that, and they did:

1. You press the button. The clock connects to your network. It genuinely
   works, which is why the screen says Connected.
2. Having connected, the WiFiManager library closes its setup page on its own
   and throws away the little web server it was using.
3. Our code then politely asked it to close the setup page as well. The library
   does not cope with being asked twice: the second time it uses the web server
   it has already thrown away, and the chip crashes and restarts.
4. The crash happened before the clock had written your network down in its own
   settings, so that was lost.
5. On start up, finding no network in its settings, the clock cleared the
   chip's own copy too, on the grounds that a stale copy would stop the setup
   page appearing. That threw away the one remaining record of your network,
   and put it straight back into setup.

Every step of that is now dealt with:

- The clock only asks the library to close the setup page if it has not
  already closed it itself. That alone stops the crash.
- Your network is written down the instant the connection succeeds, before
  anything else is allowed to happen.
- Start up still wipes the chip's copy, which is deliberate and is what makes
  the setup procedure testable. See 1.4.3 above. It was only dangerous because
  of the crash at step 3, and that is fixed.

**The button says Connect.** It is saving your network and connecting to it,
not just saving. The label is changed by the clock rather than by editing the
library.

**A failed attempt now says why.** Entering the wrong password used to drop
silently back to the waiting screen. The clock now says whether the password
was refused or the network could not be found, waits a few seconds, and returns
to the setup page so you can try again.

---

## What changed in 1.4.1

**The setup screen no longer runs off the edges.** The instructions were on one
line that measured 365 pixels on a screen with 312 usable, so 26 pixels were
cut off each side, taking half the address with it. The screen has been laid
out properly: the network name and the address each get their own line in the
larger bold face, and the explanation is broken across short lines. Every line
was measured against the font tables in the display library, and the widest is
236 pixels.

There is also a safety net. Any line that would still not fit drops to the
smaller face automatically rather than running off the edges, so this cannot
happen again if the text is ever changed or the address turns out longer, as it
would on a network using longer numbers.

**The setup screen is now live while it waits.** The portal used to be run by
the WiFiManager library, which blocks until it is finished, so the screen was
painted once and then frozen. It is now run from our own loop, which means the
display can keep up with what is going on. The important part is that the
screen tells you whether a phone has actually joined the setup network:

- **Waiting for a phone to join** means the network is up but nothing has
  connected to it yet, so the problem is at the phone's end.
- **Phone connected. Now pick your network and save** means the phone is on,
  and if no page appeared you just need to open a browser yourself.

That one distinction tells you which half of the process is stuck, which was
impossible to know from a frozen screen.

**The address shown is the real one.** It is read back from the device when the
network starts rather than being written into the message, so it is right even
if it is ever not 192.168.4.1.

**A few captive portal settings are now switched on explicitly:** answering
every address so phones offer the page, and not timing out while a phone is
still connected, so the page cannot disappear while someone is typing a
password.

### If the setup page still does not open by itself

This is largely down to the phone, not the clock. A phone decides for itself
whether to show a sign in page, and a modern Android will often quietly stay on
mobile data instead, because it has worked out this network has no internet on
it. That is why the screen suggests turning mobile data off.

Whatever the phone does, browsing to the address shown on the clock always
works.

**The time has a 10 pixel margin down each side.** It is still worked out to be
as large as will fit, just inside that margin rather than hard against the edge
of the glass.

---

## What changed in 1.4.0

**The division between segments no longer thins them.** The outline used to be
traced all the way round each bar, including the two long flat sides. Those
sides face either the background or the hole in the middle of the numeral, so
tracing them achieved nothing except eating into the bar, and the wider
settings made the numerals visibly skinnier. Only the angled ends are traced
now, since those are the only places one bar meets another. At any setting from
1 to 3 the bars keep exactly the same weight and only the gap changes.

**Seconds sit below the time in 24 hour mode.** The AM and PM marker lives in
the bottom right corner, and in 24 hour mode there is no marker, so the seconds
go down there instead and the numerals get the top of the screen back. Same
thing happens in 12 hour mode if you switch the marker off.

**The Save button follows you down the settings page.** The title and Save now
sit in a bar across the top that stays put while everything below it scrolls,
so you can change something and save without scrolling anywhere. The Save
button at the bottom still works and does the same thing.

**Factory reset from the screen.** Touch and hold to bring up the status page
and there is now a **Factory reset** button at the bottom of it. Touching it
asks you to confirm on a second screen with **Erase** and **Keep it**. Erase
clears every stored setting and forgets the WiFi network, both our copy and the
one the chip keeps for itself, then restarts. The clock comes back up exactly
as it was the first time you flashed it, asking you to connect to
My_CYD_Clock.

Nothing is erased until you press Erase on that second screen, and walking away
from it cancels on its own after 25 seconds.

### A note about touch positions

This is the first thing in the firmware that cares *where* you touched rather
than just that you did. The panel reports a raw reading from its converter
instead of a pixel, and the usable range varies slightly from board to board.
The four `TOUCH_RAW` figures for your board in `src/boards.h` are the ends of
that range.

They only have to be roughly right, because the buttons are large and there is
a confirmation step. But if the reset button feels like it is in the wrong
place on your board, those are the numbers to adjust. Raising `TOUCH_RAW_LEFT`
moves touches to the left, and so on.

---

## What changed in 1.3.2

**A 1 is now drawn as just its two bars.** It never had any others. The code
was still laying out all seven in the box and then suppressing the five that
did not belong, which is why they kept finding ways to interfere. A leading 1
now goes through its own short routine that draws the two bars and nothing
else. There is nothing behind them to clear, because nothing else is ever shown
in that box.

**Which made the sizing simpler and the numerals bigger.** The box for a 1 used
to be a fifth of the numeral height, a figure picked to look about right, and
the whole line then had to be nudged sideways to make up for the empty space
inside it. The box is now exactly one bar wide, because that is all a 1 is. So
every box is exactly as wide as what gets drawn in it, centring the boxes
centres what you see, and the sideways correction is gone.

The room that frees up goes to the other numerals:

| Time | Numeral height, was | now |
|---|---|---|
| 1:30 | 156 | 168 |
| 14:21 | 118 | 122 |
| 10:38 | 118 | 122 |

**The patch from 1.3.1 is gone too.** That version worked out whether a box was
too narrow for seven bars and skipped some of them. None of that is needed once
a 1 is simply drawn as a 1, so the check has been removed rather than left
sitting there.

Switching on **Show unlit segments** now behaves consistently again: a leading
1 shows no ghosted bars, because in this layout it genuinely does not have any.

---

## What changed in 1.3.1

**A line was being drawn through the numeral 1.** The stroke was being traced
around all seven bars, including the ones that are not lit. A bar that is not
lit shows nothing, but its outline was still being drawn in the background
colour, and wherever it passed over a bar that was lit it cut a line straight
across it.

It showed up worst on a leading 1. That numeral gets a narrow box, only wide
enough for the pair of bars down its right hand side, but the five other bars
were still being laid out in there at full size. They ended up sitting directly
on top of the two that were lit, so their outlines put a line right through the
numeral.

Two changes. The outline is now traced only around bars you can actually see,
so a bar showing nothing can no longer damage a lit neighbour. And in the
narrow box only the two bars that belong there are drawn at all, with the rest
of the box simply cleared.

The order of the drawing also matters and is now fixed: unlit fills, unlit
outlines, lit fills, lit outlines. Filling the lit bars after the unlit
outlines means no stray outline can survive on top of a lit bar unless that bar
drew it itself.

One consequence worth knowing: with a leading 1, switching on **Show unlit
segments** will not show the ghosted bars in that one narrow box, because there
is no room for them. Every other numeral is unaffected.

---

## What changed in 1.3.0

**The segments are separated by a proper stroke, and it is a setting.** Under
the clock face on the settings page there is now **Line between segments**,
with none, 1, 2 or 3 pixels. One pixel is the default and is the thinnest line
the panel can draw.

It is done the way a drawing program does it. Each segment is filled at full
size, then its outline is traced in the background colour. Two neighbouring
segments share an edge, so each of them draws over the same line and one clean
dark division appears between them. The segments keep their full size, so you
get the definition without the numerals getting lighter.

**The segments now tile properly.** They fit together into one outline with the
outer corners chamfered at 45 degrees, matching the layout printed on a real
display. Setting the line width to none gives a solid numeral with no seams
anywhere, which is the proof that they tile exactly rather than overlapping.

**The shapes are symmetrical.** They always were on paper, but they were being
shrunk by fractional amounts and the diagonal ends needed a different
correction from the flat sides, so the corners rounded off inconsistently and
one end could come out a pixel different from the other. Every corner is now a
whole number worked out from the centre line of the bar, and the numeral height
and bar thickness are both forced even so nothing lands on a half pixel. A bar
measures the same at one end as at the other by construction.

---

## What changed in 1.2.1

**The numerals are drawn properly now.** The 1 was coming out notched, and the
0 had wedges bitten out of both sides at mid height. Same cause for both, and
it was not a spacing problem.

Neighbouring bars meet at a mitred corner, so their tapered ends share a few
pixels. Whichever bar is painted second owns those pixels. The seven bars were
being painted in one fixed order, which meant an unlit bar could rub out the
corner of a lit bar that had already been drawn. The middle bar was painted
last of all, which is why an unlit middle bar chewed a wedge out of both sides
of a 0. On a leading 1 it was worse, because in the narrow box the unlit bars
on the left overlap the lit bar on the right, so they clipped its edge and left
the serif looking indent.

Every unlit bar is now painted first and every lit bar second, so a lit bar
always wins the shared pixels. Nothing was resized to achieve it.

The small dark nick you will still see on the outer edge of a 0 at mid height
is meant to be there. That is where the top right and bottom right bars meet,
and a real LED display has the same thin dark line.

**Centred on the bars, not the boxes.** A 1 sits against the right hand side of
its box, so the left of that box is empty and nothing is ever drawn in it.
Centring by boxes therefore left the visible numerals slightly right of middle.
The whole block now shifts left by half that empty space when the hour starts
with a 1, so what your eye sees is centred.

---

## What changed in 1.2.0

**The time is centred, and larger again.** It was not centred before, and there
was a reason. A 1 lights only the two bars on the right hand side of its box,
so any time starting with one left a wide empty gap on the left and the whole
display looked pushed to the right. In 12 hour mode that is one, ten, eleven and
twelve o'clock, which is a third of the day. A leading 1 now gets a narrow box
of its own, the way a real digital clock does it. That balances the display and
frees up room, so the numerals grew as well. The side margins and the spacing
between numerals were tightened at the same time.

Roughly what you get now, against 130 before:

| Time | Numeral height |
|---|---|
| 1:23 | 156 |
| 9:41 | 136 |
| 12:34 | 119 |

**AM and PM moved to the bottom right**, opposite the date. The seconds moved up
to the top right to make room.

**Forget WiFi network actually forgets now.** It genuinely did not work, and
here is why. There are two copies of your network name and password. Ours, in
the clock's own settings, and a second one the chip keeps for itself in a
different place. The button cleared ours but left the chip's, so at the next
start up the chip quietly reconnected on its own and nothing appeared to have
happened. The button now clears both. The wipe is done a moment after the reply
goes to your browser, because dropping the network any earlier would mean you
never saw the confirmation.

There is also a safety net: if the clock starts up with no network in its own
settings, it clears the chip's copy before opening the setup page, so a stale
copy can never hide the setup screen.

**The first time setup got some attention.** See the section below.

---

## Testing the setup from scratch

This is the first thing anyone else who tries this firmware will see, so it is
worth checking yourself. You do not need to erase the board to test it.

1. Open the settings page and press **Forget WiFi and restart**.
2. The clock restarts and the screen says to connect your phone to
   **My_CYD_Clock**.
3. On your phone, open the WiFi list and join that network. It is open, with no
   password. Your phone will warn you there is no internet on it. That is
   expected, stay connected.
4. A setup page should open by itself within a few seconds. If it does not,
   open a browser and go to **http://192.168.4.1**. The screen tells you this
   too, which is why it is written there.
5. Pick your network from the list, type the password, and save.
6. The screen says **Thank you, joining your network**, then shows the address
   to browse to for the full settings.

A few things worth knowing while testing:

- The setup page waits as long as it takes when the clock has never been on a
  network. If it does have one saved, the setup page closes after three minutes
  and it has another go at the saved network first, in case that network was
  only briefly away.
- Some phones need you to turn mobile data off before the setup page will open
  by itself, because the phone works out there is no internet on this network
  and quietly switches back to mobile. Going to 192.168.4.1 by hand always
  works.
- The log keeps a line for each stage, and it survives the restart, so you can
  read afterwards what happened.

---

## What changed in 1.1.1

**Thinner segment bars.** The bars were 17 percent of the numeral height, which
at the new larger sizes made them run into each other and stop reading as a
segment display. They are now 12 percent with a wider dark space where two bars
meet, and the numerals are a little narrower and further apart. That matches
the proportions of a real LED digit.

**The weather headings are right on each screen.** The age of the reading only
belongs on the current conditions screen and now only appears there. The next
hours screen shows the place name on the right instead, and the day by day
screen has proper high and low column headings lined up over the numbers, which
is what the age text was sitting on top of. The note that says a fetch is
running now goes on the left next to the title, where there is nothing behind
it on any of the three screens.

**Restarting on its own.** See the section below.

---

## Why the clock was restarting, and what now happens

The most likely cause was the clock restarting itself on purpose. Version 1.0.0
had this rule: if WiFi is not connected for two minutes, restart. That is far
too blunt. A router rebooting, a channel change, a lease renewal, or a moment of
congestion can all drop a device for two minutes, and every one of those would
have restarted the clock. The start up screen you saw appearing is exactly what
that looks like from the outside.

That rule is gone. What happens now:

- Not connected for 30 seconds: quietly asks to rejoin the network, and keeps
  asking every 30 seconds.
- Still not connected after ten minutes: restarts, because at that point
  something is genuinely wrong.

Both of those write a line to the log saying what happened and how long the
network had been gone.

### If it still restarts, this will say why

Guessing is not good enough, so the clock now keeps records that survive a
restart, in the small pocket of memory the chip does not wipe on reboot.

The **Restarts** line in the status box on the settings page shows how many
times it has rebooted since it was last unplugged, and the reason the chip
itself gives. That reason is the useful part, because it separates the
possibilities cleanly:

| What it says | What it means |
|---|---|
| the program asked for a restart | Something in the firmware called for it, and the log will say which thing |
| the program crashed | A software fault. The kept log will show what it was doing |
| task watchdog, something stopped responding | Part of the program got stuck |
| the power supply dipped | Not software at all. A weak USB supply or cable |
| powered on | A genuine power cycle, nothing to explain |

The activity log also keeps the lines from the run **before** the last restart,
shown at the top under its own heading. So whatever the clock was doing in the
seconds before it rebooted is still there for you to read or download
afterwards.

On top of that, every weather fetch now logs the free memory, the largest
single free block, and how much stack the weather task had left over. If the
restarts turn out to be memory related, those three numbers will show it
shrinking run after run.

**So: leave it overnight, then open the settings page and download the log.**
Between the restart reason and the kept lines, that should tell us exactly what
is going on.

One other thing already helping here: since 1.1.0 the forecast is fetched over
a plain connection rather than an encrypted one, and the encrypted code path was
the heaviest thing the clock did for memory. The weather task also got more
stack to work with in 1.1.1.

---

## What changed in 1.1.0

**The time is as large as it can be.** The seven segment numerals are no longer
a fixed font. The program draws them itself out of filled bars, so they can be
any size at all. It works out the tallest numeral that still fits the width
available and uses that. In 12 hour mode between one and nine o'clock there is
one numeral fewer on screen, so the digits grow to about 137 pixels tall
against the 96 they were stuck at before. In 24 hour mode they grow to about
106.

A side effect worth knowing: because it truly maximises, the numerals change
size when the hour goes from 9 to 10 and back from 12 to 1. That is twice in a
twelve hour cycle. If you would rather they stayed one size all day, say so and
I will pin them to the two numeral size.

**Better clock faces.** The old alternatives were three variations on the same
plain bitmap font. They are gone. The choices now are the seven segment face
plus four real typefaces: bold sans, bold serif, typewriter, and italic sans.
These are proper outline faces, and the program picks the largest size of each
that fits, the same way it does for the seven segment face.

**Unlit segments.** New checkbox under the clock face. It paints the bars that
are not lit in a dim version of the text colour, the way a real LED display
shows its dark segments. Off by default.

**Bigger, bolder AM, PM and date.** Both now use the same bold typeface at the
same size.

**The weather is fetched when you tap, not on a timer.** See the section below.

**New weather pictures.** They are drawn from scratch rather than being stock
shapes, using the display library's anti aliased drawing. Sun, moon, cloud,
part cloud with sun or moon, rain, snow, fog, and thunder. The cloud is drawn
as a dark outline with the fill on top, which hides the stair stepping you get
on a diagonal edge and gives it a clean flat look.

---

## What changed in 1.0.1

**The weather works now.** The forecast server sends its reply in pieces with a
size marker in front of each piece, and no total length given up front. Version
1.0.0 handed that raw stream straight to the part of the program that reads
JSON. It read the first size marker as a number, decided it had a complete
answer, and stopped. That is why the screen said the reply was not understood
rather than reporting a connection problem. The reply is now reassembled before
it is read.

**There is an activity log.** The settings page has a panel near the bottom
showing the last 50 things the clock did, with a time against each one. It
updates while you have the page open, and there are buttons to refresh it,
clear it, and download it as a text file. The same lines also go to the serial
monitor. Every step of a weather fetch is logged, including the reply size, any
server error code, and the first part of a reply that could not be read.

The log lives in memory only, so a restart empties it.

**Three compiler warnings are gone.** Two were mine, a harmless type mismatch in
the status screen colours. The third came from the display library complaining
that its own touch support was not configured, which is correct and intended,
since the touch panel is driven by a separate library on a separate bus. That
one is now switched off with `DISABLE_ALL_LIBRARY_WARNINGS` in
`platformio.ini`.

---

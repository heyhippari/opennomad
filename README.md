# OpenNomad

An open-source reimplementation of Omikron: The Nomad Soul, based on reverse engineered behaviour from the original game.

## Disclaimer

This is a reverse engineering project. NO COPYRIGHTED FILES FROM THE ORIGINAL GAME ARE SHARED.

You need a legal copy of the original game in order to run this.

Omikron: The Nomad Soul is the property of Square Enix and Quantic Dream.

## AI Usage Disclaimer

AI was used to help with the reverse engineering process: helping to parse file formats, analyze disassembled functions, help keep track of structs and even find functions Ghidra and OOAnalyzer didn't.

AI was also used for implementation of this SDL3 port. I am primarily a web developer. I do TypeScript, I've done some Rust, quite a bit of Go, a lot of Python. I have very vague notions of C++ and have only practiced C during my teens (as my first programming language aside from typing in BASIC listings on an Amiga 500 before my family got a PC). I still check things, I review code, I guide the agents on architecture, what I'm looking for, what to test against, how to ensure things are implemented correctly based on the reverse engineered specs. But obviously, I am not a C++ developer by trade, so there will no doubt be parts of the code that are dirty, inefficient, badly designed.

## Why ?

The Nomad Soul is one of the first games I played on Windows PCs, back in the late 90s. I remember spending weeks trying to figure out how to mess with the game's files, exploring the various cities, listening to the songs by David Bowie.

It's a janky game, in many respects a bad game, but it's a childhood memory I still cherish. Over the years, the game has developed issues due to technology moving forward.

Every few years, I would remember [a post by David Cage on the late French forum Mayerem](https://web.archive.org/web/20071016020039/http://www.mayerem.com/topic-1158.html#post76174) about open sourcing the game, back in 2006. It never came to pass, but I always wondered "what if".

So I did it myself. Both to finally fulfil the promise David Cage made in 2006 and to ensure the game can keep running on new hardware, different operating systems, and be enjoyable for whoever holds the same weird fondness I have for this questionable piece of software.

## Status

This is EXTREMELY early. I basically started by loading some hardcoded 3DO/3DT files in a scene, and expanded the scope from there.
I am starting with the engine's initialization, because most of the game seems to be script-based. As of the commit this revision of the README is a part of, we should be close to reaching the main menu.
A lot of things beyond that are already reverse engineered (As tends to happen when reversing software, you pull on a thread that might or might not be related to what you're trying to find), but not implemented yet, as much context is still missing.

Currently, we can reach the main menu (as well as fix the black background on the game's logo, and improve the layout for larger resolutions of today!), with the intro videos, the splash screen (slightly "improved" with a fade in/out) and music working.

![Screenshot of the modernized main menu](.github/screenshots/title_screen.png)

Next steps: handle the `Quit` menu entry, improve the presentation of the menu a bit more with a very quick fade in/fade out, then start working on the game's introduction.

## How to run

There's not much to run at the moment, but if you're really motivated, follow the instructions in [docs/QuickStart.md](the quick start documentation).

You can then either copy the original files next to the executable or set an environment variable names `OPENNOMAD_GAME_DATA_ROOT` to the root folder of the game (Where `Runtime.exe` is located).

<div align="center">

<img src="SpliceLogo.png">

The Current Version of Splice is: 1.0.0

<tr>
    <td>
        <a href="https://github.com/sinhateam"><img src='https://img.shields.io/badge/A Sinha-Product-blue.svg'/></a>
    </td>
</tr>

[![CI](https://img.shields.io/github/actions/workflow/status/Open-Splice/Splice/main.yml?branch=main&label=Splice%20CI%2FCD&logo=github&style=plastic)](https://github.com/Open-Splice/Splice/actions/workflows/main.yml)

[![Live on GitBook](https://img.shields.io/badge/Live%20on-Gitbook-black.svg?style=plastic&logo=gitbook&logoColor=white)](https://sinha.gitbook.io/splice)

[![Reddit](https://img.shields.io/badge/On-Reddit-%23FF4500.svg?style=plastic&logo=Reddit&logoColor=white)](https://www.reddit.com/r/OpenSplice/)

[![Gitter chat](https://badges.gitter.im/gitterHQ/gitter.png)](https://app.gitter.im/#/room/#splice:gitter.im)

[![CodeQL](https://github.com/Open-Splice/Splice/actions/workflows/codeql.yml/badge.svg)](https://github.com/OpenSplice/Splice/actions/workflows/codeql.yml)

# The Splice Programming Language

Splice is an Open-Source, high-level, Dynamic Programming language developed by Open-Splice, A Sinha Group organisation, to make writing code for embedded systems easier. This is the main Github repo where all source code of Splice remains. Installation of Splice can be found below.

## Whats in the world

Programming embedded systems today is dominated by **C and C++**, which offer excellent performance but come with a steep learning curve and complex memory management.

Higher-level languages such as Lua aim to simplify development, but VM complexity and bytecode handling can become challenging on constrained systems.

Splice explores an alternative design that prioritizes **clarity, simplicity, and inspectable execution** while remaining suitable for embedded environments.

## Why Splice

Splice is designed to:

- Reduce VM complexity
- Keep execution transparent and debuggable
- Maintain a small, understandable core
- Make embedded-focused language tooling easier to reason about

This project is **early-stage and experimental**, focused on validating design decisions before expanding features.

## How does Splice work?

Splice does not use traditional numeric opcode-based bytecode
(e.g. `0xA1 → LOAD`).

Instead, it uses **KAB (Keyword Assigned Bytecode)**:

- Bytecode instructions are represented using readable keywords
- The VM directly interprets these semantic instructions
- This simplifies debugging and tooling around bytecode

This design reduces interpreter complexity and makes the bytecode easier to inspect and understand.

The current SPVM runtime has been tested on desktop platforms and is designed with microcontrollers such as the **ESP32** in mind.

## How to Install?

It is recomended to use our ```build.sh``` and not any third party ```build.sh``` as they could cause harm to your system.
To run Build.sh run
``` ./build.sh ```.
If you have a corrupted version of Splice run ```./build.sh --force``` to forcefully rewrite the corrupted version with the right one.

## Source Code Orginization

Splice is orginized in this manner

| Directory         | Contents                                                           |
| -                 | -                                                                  |
| `src/`            | Code to run the SPVM (Splice Virtual Machine) onto systems like Microcontrollers                         |
| `examples/`        | Example code that works with Splice                                              |

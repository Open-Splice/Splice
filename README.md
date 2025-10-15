<div align="center">

<img src="SpliceLogo.png">

The Current Version of Splice is: 1.0.0

<tr>
    <td>
        <a href="https://github.com/sinhateam"><img src='https://img.shields.io/badge/A Sinha-Product-blue.svg'/></a>
    </td>
</tr>

# The Splice Programming Language

Splice is a Open-Source, High Level, Dynamic Programming language developed by Open-Splice, A Sinha Group Orginization to make writing code for embedded systems easier. This is the main Github repo where all source code of Splice remains. Installaition of Splice can be found below.

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

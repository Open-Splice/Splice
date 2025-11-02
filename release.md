# Release notes

Current Version: 1.0.0

## Overview

This Version of Splice has fixed major bugs in Splice. You can Also create an Ai model like this.

```
func sigmoid(x) {
    let negx = 0 - x
    let exp_approx = 1 + negx + (negx * negx) / 2 + (negx * negx * negx) / 6
    let s = 1 / (1 + exp_approx)
    return s
}

func train() {
    let w1 = 0.5
    let w2 = -0.5
    let bias = 0.0
    let lr = 0.1

    let data_x1 = [0, 0, 1, 1]
    let data_x2 = [0, 1, 0, 1]
    let data_y  = [0, 0, 0, 1]

    for epoch in 0..50 {
        for i in 0..3 {
            let x1 = data_x1[i]
            let x2 = data_x2[i]
            let y  = data_y[i]

            let z = (x1 * w1) + (x2 * w2) + bias
            let pred = sigmoid(z)
            let error = y - pred

            w1 = w1 + (lr * error * x1)
            w2 = w2 + (lr * error * x2)
            bias = bias + (lr * error)
        }
    }

    print("trained weights:")
    print("w1=" + w1 + " w2=" + w2 + " bias=" + bias)

    print("Predictions:")
    print("0,0 -> " + sigmoid((0*w1)+(0*w2)+bias))
    print("0,1 -> " + sigmoid((0*w1)+(1*w2)+bias))
    print("1,0 -> " + sigmoid((1*w1)+(0*w2)+bias))
    print("1,1 -> " + sigmoid((1*w1)+(1*w2)+bias))
}

train()
```

While this is a Basic AI in Splice and isn't really a great AI like the other ones you might have seen, this is a really basic system and can be embedded on microcontrollers including Qualcomm's Arduino Q.

## Parser / Syntax Fixes

- Fixed missing semicolon handling inside `}` blocks.  
- Allowed `}` to terminate loops and functions even without a semicolon after the last statement.  
- Fixed `Expected '}' after for body` parser issue by aligning brace/semicolon scanning logic.  
- Improved `parse_statement()` to correctly handle nested `for` and `while` loops.  
- Ensured `if ... else` parsing doesn’t misread `else` as standalone token.  
- Fixed `Expected expression` error after `import` by verifying `IMSTRING` + `SEMICOLON` order.  

---

## Bytecode Builder (Compiler)

- Corrected `import` handling to support both `"file.spbc"` and bare identifiers like `import amath;`.  
- Added proper handling for `OP_IMSTRING` in import blocks.  
- Fixed missing opcodes (0x35–0x69 range) — now correctly emits all brackets and punctuation.  
- Corrected string literal writing using length-prefix (`unsigned short`).  
- Fixed missing null terminator in string copy.  

---

## Virtual Machine (Runtime)

- Resolved segmentation fault in nested loop execution (fixed stack overflow in block parsing).  
- Fixed safe array access in `inputs[i][j]`-like syntax to prevent null dereference.  
- Prevented infinite recursion in pseudo-`sigmoid()` approximations.  
- Fixed crash when calling user functions before global variable resolution.  

---

## Math / Logic Layer

- Added polynomial approximation of `exp(x)` for `sigmoid()` (safe and fast).  
- Verified floating-point arithmetic stability across multiple epochs.  
- Improved precision of learning updates in `train()` (prevented NAN or overflow).  

---

## 🧰 Debugging / Developer Tools

- Added `[DEBUG]` print statements for token scanning (`token[%d] = %s`).  
- Added debug trace for import flow showing tokens after import.  
- Added opcode dump verification (`[BC] opcode ...`) for debugging compiler output.  
- Introduced `[INFO]`, `[ERROR]`, `[SUCCESS]` prefixes in VM for clarity.  

---

## 🚀 AI Training Example Stability

- Fixed training code segmentation fault caused by large epoch count in nested loops.  
- Replaced nested list `[[0,0], ...]` with separate arrays `data_x1`, `data_x2`, and `data_y`.  
- Fixed unstable sigmoid producing 0.5 plateau by reworking the approximation.  
- Confirmed successful model convergence with valid output.  

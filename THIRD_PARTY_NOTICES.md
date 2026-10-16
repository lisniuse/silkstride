# Acknowledgments / 致谢

Silkstride's articulated walking mechanics were inspired by **Rainbow Spider**
by **run-time**: <https://codepen.io/run-time/pen/abYeqZ>.
The reference helped inform the contact order `(legIndex * 3) % 8` and the
Verlet distance/angle constraint approach. The desktop application is implemented
in native C++; it does not embed the Pen or require its JavaScript libraries.

丝行的关节行走机制参考 run-time 的 Rainbow Spider，包括换脚顺序与 Verlet 距离、
角度约束的思路。桌面程序使用原生 C++ 实现，不嵌入该网页或依赖其 JavaScript 库。

CodePen's [licensing documentation](https://blog.codepen.io/documentation/licensing/)
states that public Pens are MIT licensed. The following notice credits the
reference and accompanies any adapted portions of its walking mechanics.

## Rainbow Spider — MIT notice

Copyright (c) run-time (https://codepen.io/run-time)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

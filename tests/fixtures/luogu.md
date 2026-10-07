# 洛谷语法测试

:::info[这是一个提示]
我是提示内容。
:::

:::success[我默认处于展开状态]{open}
我是展开内容。
:::

:::::warning[我是父容器]

我是一些文字。

::::success[我是子容器 1]{open}
我是内容 1。
::::

:::::

:::align{left}
居左内容。
:::

:::align{center}
居中内容。
:::

:::align{right}
居右内容。
:::

:::epigraph[——otto]
大家好啊，我是说的道理。
:::

::cute-table{three}

| 测试点编号 | n≤ | m≤ | 特殊性质 |
| --- | --- | --- | --- |
| 1~2 | 100 | 100 | 无 |
| 3~4 | 100 | 100 | 无 |

::cute-table{tuack=2}

| A | B | C | D |
| --- | --- | --- | --- |
| 1 | 2 | 3 | 4 |

| 测试点编号 | n≤ | m≤ | 特殊性质 |
| --- | --- | --- | --- |
| 1~2 | 100 | 100 | 无 |
| 3~4 | ^ | ^ | 无 |
| 5 | 10^5 | < | A |

```cpp lines=2-3,5
#include <bits/stdc++.h>
using namespace std;
int main(){
  int a, b;
  cin >> a >> b;
  cout << a + b << endl;
  return 0;
}
```

```
int bare = 42;  // Luogu highlights a bare fence as C++
```

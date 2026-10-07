# AT\_abc397\_d \[ABC397D] Cubes 题解

题意：给定 $N$（$N \le 10^{18}$），求出一对正整数 $X,Y$ 满足 $X^3-Y^3=N$，或报告无解。

## 一开始的思路：暴力

枚举 $Y$，求是否有满足条件的 $X$，如果快要超时就报告无解。

```cpp
bool is_cbr(ll s) {
        ll cbr = cbrt(s);
        return(cbr * cbr * cbr == s);
}

int main(){
        cin >> n;
        for(ll i = 1; i <= 1e6; i++) {
                if(is_cbr(n + i * i * i)) {
                        cout << cbrt(n + i * i * i) << ' ' << i;
                        return 0;
                }
        }
        cout << -1;
        return 0;
}
```

十个测试点 WA ，原因：精度误差、long long 范围。

## 正确的思路

这个 $X^3-Y^3$ 看起来很棘手，我们将它进行因式分解。将其看作两个立方体体积之差，如图：

![](https://cdn.luogu.com.cn/upload/image_hosting/divh65sj.png)

剩下的体积我们将其分拆：

![](https://cdn.luogu.com.cn/upload/image_hosting/z99zhoh5.png)

![](https://cdn.luogu.com.cn/upload/image_hosting/uv2cu9v4.png)

红色体积为 $x^2 \times (x-y)$，绿色体积为 $x \times y \times (x-y)$，蓝色体积为 $y^2 \times (x-y)$。

相加得：$X^3-Y^3=x^2 \times (x-y) + x \times y \times (x-y) + y^2 \times (x-y) = (x-y)(x^2+xy+y^2)$ 有了因式分解，就可以枚举 $x-y$ 了。

设 $t$ 为 $x-y$，因为 $t^2=x^2-2xy+y^2 < x^2+xy+y^2$，所以 $t^2 \times t < t(x^2+xy+y^2) = N$，由此可知 $t^3 < N$。

知道了 $t$ 的范围，就可以开始判定 $t$ 是否合法了。

如果 $N$ 不能被 $t$ 整除，非法。

$t^2=x^2-2xy+y^2，N \div t=x^2+xy+y^2$，两式相减，可得：

$$(x^2+xy+y^2) - (x^2-2xy+y^2) = (x^2 - x^2) + (xy + 2xy) + (y^2 - y^2) = 3xy$$

所以 $N \div t - t^2 = 3xy$。如果式子的结果不能被 $3$ 整除，必然不合法。

知道了 $x-y,xy$，方程就好解了。

$\begin{aligned}
 (x-y)^2+4xy &= x^2-2xy+y^2+4xy \\
 &= x^2+2xy+y^2 \\
 &= (x+y)^2
\end{aligned}$

现在已知 $x+y,x-y$，就可以轻松求出 $x$ 与 $y$ 了。

## 代码：

```cpp
#include<bits/stdc++.h>

using namespace std;
using ll = long long;

const int MAXN = 3e5 + 35, MAXT = 1e6;

ll n;

int main(){
        cin >> n;
        for(ll i = 1; i * i * i < n; i++) {//i = x - y;
//              cout << i << ' ';
                if(n % i)continue;
                ll j = n / i - i * i;//j = 3xy;
//              cout << j << ' ';
                if(j % 3)continue;
                j /= 3;
                ll x_add_y = sqrt(i * i + j * 4);//x_add_y = x + y;
//              cout << x_add_y << '\n';
                if((i + x_add_y) % 2)continue;
                if(x_add_y - i < 2)continue;//如果 y = 0，不符合题意。
                ll x = (i + x_add_y) / 2, y = (x_add_y - i) / 2;
                if(x * x + x * y + y * y == n / i){ //最后判断一下
                        cout << x << ' ' << y;
                        return 0;
                }
        }
        cout << -1;
        return 0;
}
```

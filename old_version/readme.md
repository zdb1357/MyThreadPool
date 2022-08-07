

1. 编译成动态库

```cpp
g++ -fPIC -shared threadpool.cpp -o libtdpool.so -std=c++17
```

2. 移动so到lib下

```
mv libtdpool.so /usr/local/lib/
```

3. 移动h文件到include下

```
mv threadpool.h /usr/local/include/
```

4. 测试

```
g++ test.cpp -std=c++17 -ltdpool -lpthread
```

```
./a.out
```


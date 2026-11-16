# 编译检查用的桩头文件

这些头文件只声明了本项目用到的 libdrm / turbojpeg / Linux uapi 接口，
目的是在**没有安装开发包**的机器上做语法/类型检查：

```bash
make stubs-check
```

它们**不是**真实的库头文件，**不要**用于正式构建。正式构建请安装
`libdrm-dev`、`libturbojpeg0-dev` 并使用系统头文件。

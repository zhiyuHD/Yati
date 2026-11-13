#ifndef YATI_VERSION_H_
#define YATI_VERSION_H_

// 版本号的唯一来源是仓库根的 VERSION 文件，由构建系统通过编译期宏注入：
//   CMake : target_compile_definitions(yati PRIVATE YATI_VERSION="...")
//   Yubild: yubild.cmake('add_compile_definitions(YATI_VERSION="...")')
// 不走构建系统、直接手编某个 .cpp 时才会用到下面的兜底值，它不冒充发布版本。
#ifndef YATI_VERSION
#define YATI_VERSION "dev"
#endif

// 产品标识：显示名用于标题栏与通知，app id 用于缓存目录等路径。
#ifndef YATI_PRODUCT_NAME
#define YATI_PRODUCT_NAME "Yati"
#endif

#ifndef YATI_APP_ID
#define YATI_APP_ID "yati"
#endif

#endif  // YATI_VERSION_H_

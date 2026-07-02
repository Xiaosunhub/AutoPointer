AutoPointer - C++ 集合智能指针

AutoPointer 是一个集合了 unique/shared/weak 模式的多功能智能指针

##功能特性
- 集成多功能指针：集成unique/shared/weak/costum自定义指针
- *解引用
- ->调用
- 自动释放

##安装指南
#前提条件
- C++ 17 或更高版本 (/std:c++17)

##安装步骤

下载 autoPointer.h 头文件
将文件复制到 Visual Studio 包含目录中，例如：
[安装路径]\Microsoft Visual Studio\2022\Community\VC\Auxiliary\VS\include

在项目中包含头文件：
#include <autoPointer.h>

##替代安装方式

也可以直接将头文件放在项目目录中，并使用相对路径包含：
#include "path/to/autoPointer.h"

##使用示例

#include <autoPointer.h>

int main() {
	// 创建智能指针
	auto_ptr::autoPointer<int, auto_ptr::_PubPTR::unique> ptr = nullptr;

        // 解引用
	*ptr;
	// (*ptr).somemethod();
	// ptr->somemethod();
        
        // 自动释放，但如果用_Exit或者__debugbreak终止进程还是会造成泄露
	return 0;
}


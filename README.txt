README

编译源程序：项目目录下 gcc -o webserver webserver.c -lpthread

运行源程序：项目目录下 ./webserver

访问方式：
1. 网站登录名为3150102238，密码为2238
2. 浏览器地址栏中输入 127.0.0.1:2238/test.txt 访问纯文本文件
3. 浏览器地址栏中输入 127.0.0.1:2238/noimg.html 访问不包含s图片的HTML文件
4. 浏览器地址栏中输入 127.0.0.1:2238/test.html 访问包含图片和文本的HTML文件

项目文件组织结构：
1. webserver.c是服务器程序的源文件
2. webserver是服务器程序在Linux平台下的可执行文件
3. file目录包含了服务器的静态文件：
    (1) file/txt/test.txt是纯文本文件
    (2) file/img/logo.jpg是图片文件
    (3) file/html/noimg.html是不包含图片的HTML文件
    (4) file/html/test.html是包含图片的HTML文件
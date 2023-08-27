# ?= g++ 表示如果 CXX 还没有被定义过（在命令行或之前的 Makefile 中），那么将其设置为默认值 g++
CXX ?= g++

# ?= 1 表示如果 DEBUG 还没有被定义过，将其设置为默认值 1（开启调试模式）
DEBUG ?= 1
ifeq ($(DEBUG), 1)
# 这个选项告诉编译器生成用于调试的符号信息，以便在调试过程中能够查看变量值、函数调用栈等
    CXXFLAGS += -g
else
# 将 -O2 选项添加到编译器选项中,这个选项表示开启优化，可以让生成的代码在执行速度上更快 
    CXXFLAGS += -O2

endif

server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp  ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
# Makefile

# 编译器
CXX = g++

# 目录
BIN_DIR = ../bin

# 编译选项
CXXFLAGS = -std=c++11 -I../include -Wall -Wextra -O2

# 链接选项
# 如果wfrest库安装在标准路径，可以直接使用-lwfrest
# 如果安装在自定义路径，例如 /opt/wfrest/lib，则需要添加 -L/opt/wfrest/lib
LDFLAGS = -lwfrest -lworkflow -ltinyxml2

# 目标文件
all: $(BIN_DIR)/wfrest_server

# 构建 wfrest_server
$(BIN_DIR)/wfrest_server: Configuration.cpp Dictionary.cpp KeyRecommend.cpp NetServer.cpp SearchEngineServer.cpp SplitToolCppJieba.cpp WebPage.cpp WebSearch.cpp main.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/wfrest_server \
		Configuration.cpp \
		Dictionary.cpp \
		KeyRecommend.cpp \
		NetServer.cpp \
		SearchEngineServer.cpp \
		SplitToolCppJieba.cpp \
		WebPage.cpp \
		WebSearch.cpp \
		main.cpp $(LDFLAGS)

# 清理目标
clean:
	rm -f $(BIN_DIR)/wfrest_server

# 声明伪目标
.PHONY: all clean

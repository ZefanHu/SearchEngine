# Makefile

# 编译器
g++ = g++

# 目录
SRC_DIR = .
INC_DIR = ../include
BIN_DIR = ../bin

# 编译选项
CXXFLAGS = -std=c++11 -I../include -Wall -Wextra -O2

# 链接选项
LDFLAGS = -ltinyxml2

# 目标文件
all: $(BIN_DIR)/test_configuration $(BIN_DIR)/test_split_tool_cpp_jieba $(BIN_DIR)/test_dict_producer $(BIN_DIR)/test_dir_scanner $(BIN_DIR)/test_page_lib_preprocessor

# 测试 Configuration
$(BIN_DIR)/test_configuration: $(SRC_DIR)/Configuration.cpp
	mkdir -p $(BIN_DIR)
	$(g++) $(CXXFLAGS) -DCONFIGURATION_TEST_MAIN -o $(BIN_DIR)/test_configuration $(SRC_DIR)/Configuration.cpp $(LDFLAGS)

# 测试 SplitToolCppJieba
$(BIN_DIR)/test_split_tool_cpp_jieba: $(SRC_DIR)/SplitToolCppJieba.cpp $(SRC_DIR)/Configuration.cpp
	mkdir -p $(BIN_DIR)
	$(g++) $(CXXFLAGS) -DTEST_SPLIT_TOOL_CPP_JIEBA -o $(BIN_DIR)/test_split_tool_cpp_jieba $(SRC_DIR)/SplitToolCppJieba.cpp $(SRC_DIR)/Configuration.cpp $(LDFLAGS)

# 测试 DictProducer
$(BIN_DIR)/test_dict_producer: $(SRC_DIR)/DictProducer.cpp $(SRC_DIR)/Configuration.cpp $(SRC_DIR)/SplitToolCppJieba.cpp
	mkdir -p $(BIN_DIR)
	$(g++) $(CXXFLAGS) -DTEST_DICT_PRODUCER -o $(BIN_DIR)/test_dict_producer $(SRC_DIR)/DictProducer.cpp $(SRC_DIR)/Configuration.cpp $(SRC_DIR)/SplitToolCppJieba.cpp $(LDFLAGS)

# 测试 DirScanner
$(BIN_DIR)/test_dir_scanner: $(SRC_DIR)/DirScanner.cpp $(SRC_DIR)/Configuration.cpp
	mkdir -p $(BIN_DIR)
	$(g++) $(CXXFLAGS) -DTEST_DIR_SCANNER -o $(BIN_DIR)/test_dir_scanner $(SRC_DIR)/DirScanner.cpp $(SRC_DIR)/Configuration.cpp $(LDFLAGS)

# 测试 PageLibPreprocessor
$(BIN_DIR)/test_page_lib_preprocessor: $(SRC_DIR)/PageLibPreprocessor.cpp $(SRC_DIR)/Configuration.cpp $(SRC_DIR)/SplitToolCppJieba.cpp $(SRC_DIR)/WebPage.cpp $(SRC_DIR)/DirScanner.cpp
	mkdir -p $(BIN_DIR)
	$(g++) $(CXXFLAGS) -DTEST_PAGE_LIB_PREPROCESSOR -o $(BIN_DIR)/test_page_lib_preprocessor $(SRC_DIR)/PageLibPreprocessor.cpp $(SRC_DIR)/Configuration.cpp $(SRC_DIR)/SplitToolCppJieba.cpp $(SRC_DIR)/WebPage.cpp $(SRC_DIR)/DirScanner.cpp $(SRC_DIR)/PageLib.cpp $(LDFLAGS)

# 清理目标
clean:
	rm -f $(BIN_DIR)/test_configuration $(BIN_DIR)/test_split_tool_cpp_jieba $(BIN_DIR)/test_dict_producer $(BIN_DIR)/test_dir_scanner $(BIN_DIR)/test_page_lib_preprocessor

# 声明伪目标
.PHONY: all clean

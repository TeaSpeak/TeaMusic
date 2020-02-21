#pragma once

#include <vector>
#include <string_view>
#include <string>
#include <type_traits>

//Note: I want concepts!
namespace strings {
    template <typename T>
    using enable_is_string_similar = std::integral_constant<bool, std::is_base_of<std::string_view, T>::value || std::is_base_of<std::string, T>::value>;

    template <typename T, std::enable_if_t<enable_is_string_similar<T>::value, int> = 0>
    T trim(const T& str) {
        size_t begin{0}, end{str.length()};
        while(end > 0 && str[end - 1] == ' ') end--;
        while(begin < end && str[begin] == ' ') begin++;
        return str.substr(begin, end - begin);
    }

    template <typename T, typename V, std::enable_if_t<enable_is_string_similar<T>::value, int> = 0>
    V split_lines(const T& str) {
        V result{};
        split_lines(result, str);
        return result;
    }

    template <typename T, typename V, std::enable_if_t<enable_is_string_similar<T>::value, int> = 0>
    void split_lines(V& result, const T& str) {
        size_t index = 0;
        do {
            auto found = str.find('\n', index);
            result.push_back(str.substr(index, found - index));
            index = found + 1;
        } while(index != 0);
    }
}
// snapshoter.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

inline std::wstring operator/(const std::wstring &d, const std::wstring &f) {return d.back() == L'/' ? d + f : d + L'/' + f;}
inline std::wstring operator/(const std::wstring &d, const wchar_t      *f) {return d.back() == L'/' ? d + f : d + L'/' + f;}

class DirEntry
{
public:
    DirEntry *parent = nullptr;
    std::map<std::wstring, DirEntry> subdirs;
    std::map<std::wstring, uint64_t> files;
    int64_t size = 0; // total size of files including subdirectories
} root;

const int64_t min_trackable_file_size = 1024*1024;
uint64_t occupied_bytes;
int progress_bar_size = 64, progress_cur_pos = 0;

void enum_files_recursively(const std::wstring &dir_name, DirEntry &de)
{
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile((dir_name / L"*.*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) // skip symbolic links
            continue;

        std::wstring file_name(fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (file_name == L"." || file_name == L"..")
                continue;

            auto it = de.subdirs.emplace(file_name, DirEntry());
            it.first->second.parent = &de;
            //it.first->second.dir_name = &it.first->first;
        }
        else {
            uint64_t file_size = (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            de.size += file_size;
            if (file_size >= min_trackable_file_size)
                de.files[file_name] = file_size;
        }
    } while (FindNextFile(h, &fd));

    FindClose(h);

    for (DirEntry *pde = de.parent; pde != nullptr; pde = pde->parent)
        pde->size += de.size;

    for (int progress_new_pos = int(root.size * progress_bar_size / occupied_bytes); progress_cur_pos < progress_new_pos; progress_cur_pos++)
        std::cout << '#';

    for (auto &&sd : de.subdirs)
        enum_files_recursively(dir_name / sd.first, sd.second);
}

int _tmain(int argc, _TCHAR* argv[])
{
    ULARGE_INTEGER free_bytes_available_to_caller, total_number_of_bytes;
    if (!GetDiskFreeSpaceEx(L"C:", &free_bytes_available_to_caller, &total_number_of_bytes, NULL))
        return -1;
    occupied_bytes = total_number_of_bytes.QuadPart - free_bytes_available_to_caller.QuadPart;

    for (int i = 0; i < progress_bar_size; i++)
        std::cout << '.';
    std::cout << '\r';

    enum_files_recursively(L"C:", root);
    std::cout << '\n';

    return 0;
}

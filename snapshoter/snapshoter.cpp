// snapshoter.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

inline std::wstring operator/(const std::wstring &d, const std::wstring &f) {return d.back() == L'/' ? d + f : d + L'/' + f;}
inline std::wstring operator/(const std::wstring &d, const wchar_t      *f) {return d.back() == L'/' ? d + f : d + L'/' + f;}

FILE *snapshot_file;

template <typename Ty> void wr(const Ty &obj)
{
    fwrite(&obj, sizeof(Ty), 1, snapshot_file);
}
void wr(const std::wstring &s)
{
    if (s.size() >= 65536)
        exit(-1);
    wr(uint16_t(s.size()));
    fwrite(s.data(), 2, s.size(), snapshot_file);
}

class DirEntry
{
public:
    DirEntry *parent = nullptr;
    std::map<std::wstring, DirEntry> subdirs;
    std::map<std::wstring, uint64_t> files;
    int64_t size = 0; // total size of files including subdirectories

    void write()
    {
        if (files.size() >= 65536 || subdirs.size() >= 65536)
            exit(-1);

        wr(size);

        wr(uint16_t(files.size()));
        for (auto &&f : files) {
            wr(f.first);
            wr(f.second);
        }

        wr(uint16_t(subdirs.size()));
        for (auto &&sd : subdirs) {
            wr(sd.first);
            sd.second.write();
        }
    }
} root;

const int64_t min_trackable_file_size = 1024*1024;
uint64_t occupied_bytes;
int progress_bar_size = 64, progress_cur_pos = 0;
std::wstring cur_dir;

void enum_files_recursively(const std::wstring &dir_name, DirEntry &de)
{
    if (dir_name == cur_dir)
        return;

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
    wchar_t cur_dir_buf[MAX_PATH];
    GetCurrentDirectory(MAX_PATH, cur_dir_buf);
    for (wchar_t *c = cur_dir_buf; *c; c++)
        if (*c == '\\') *c = '/';
    cur_dir = cur_dir_buf;

    std::wstring disk = L"C:";

    ULARGE_INTEGER free_bytes_available_to_caller, total_number_of_bytes;
    if (!GetDiskFreeSpaceEx(disk.c_str(), &free_bytes_available_to_caller, &total_number_of_bytes, NULL))
        return -1;
    occupied_bytes = total_number_of_bytes.QuadPart - free_bytes_available_to_caller.QuadPart;

    for (int i = 0; i < progress_bar_size; i++)
        std::cout << '.';
    std::cout << '\r';
    enum_files_recursively(disk.c_str(), root);
    std::cout << '\n';

    time_t t = time(NULL);
    struct tm tm;
    localtime_s(&tm, &t);
    char s[100];
    strftime(s, sizeof(s), "%Y-%m-%d", &tm);
    wchar_t fname[100];
    swprintf_s(fname, L"%c_%S_%.1fGb.snapshot", disk[0], s, free_bytes_available_to_caller.QuadPart / (1024.0 * 1024 * 1024));
    _wfopen_s(&snapshot_file, fname, L"wb");
    root.write();
    fclose(snapshot_file);

    std::cout << "OK\n";
    return 0;
}

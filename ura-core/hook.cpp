#include <stdinclude.hpp>
#include <stdint.h>
#include <./notifier/notifier.hpp>
#include <./MsgPack/msgpack_data.hpp>
#include <thread>
#include <string>
#include <vector> 

using namespace std;

namespace
{
	bool patch_game_assembly();
	void* load_library_w_orig = nullptr;
	void* set_fps_orig = nullptr;
	void* set_vSyncCount_orig = nullptr;
	void* LZ4_decompress_safe_ext_orig = nullptr;
	void* LZ4_compress_default_ext_orig = nullptr;
	void* get_DatabaseSavePath_orig = nullptr;
	void* GetMasterdataDirectory_orig = nullptr;
	void* LZ4_compress_default_ext_addr = nullptr;
	void* LZ4_decompress_safe_ext_addr = nullptr;
	void* HttpHelper_CompressRequest_orig = nullptr;
	void* HttpHelper_CompressRequest_addr = nullptr;
	void* HttpHelper_DecompressResponse_orig = nullptr;
	void* HttpHelper_DecompressResponse_addr = nullptr;

	void print_hex_dump(const char* buffer, size_t length) {
		if (!buffer) {
			printf("  [Buffer is NULL]\n");
			return;
		}
		printf("  Buffer Length: %zu (0x%zX) bytes\n", length, length);

		const int bytes_per_line = 16;
		for (size_t i = 0; i < length; i += bytes_per_line) {
			// Print address offset
			printf("  %08zX: ", i);

			// Print hex values
			for (int j = 0; j < bytes_per_line; ++j) {
				if (i + j < length) {
					printf("%02X ", static_cast<unsigned char>(buffer[i + j]));
				}
				else {
					printf("   ");
				}
			}
			printf(" ");

			for (int j = 0; j < bytes_per_line; ++j) {
				if (i + j < length) {
					char c = buffer[i + j];
					putchar(isprint(static_cast<unsigned char>(c)) ? c : '.');
				}
			}
			printf("\n");
		}
	}

	HMODULE __stdcall load_library_w_hook(const wchar_t* path)
	{
		// GameAssembly.dll code must be loaded and decrypted while loading criware library
		if (path == L"libnative.dll"s)
		{
			if (patch_game_assembly()) {
				MH_DisableHook(LoadLibraryW);
				MH_RemoveHook(LoadLibraryW);

				// use original function beacuse we have unhooked that
				return LoadLibraryW(path);
			}
		}
		return reinterpret_cast<decltype(LoadLibraryW)*>(load_library_w_orig)(path);
	}

	void set_fps_hook(int value)
	{
		return reinterpret_cast<decltype(set_fps_hook)*>(set_fps_orig)(g_max_fps);
	}

	void set_vSyncCount_hook(int value)
	{
		return reinterpret_cast<decltype(set_vSyncCount_hook)*>(set_vSyncCount_orig)(g_vertical_sync_count);
	}

	int LZ4_decompress_safe_ext_hook(
		char* src,
		char* dst,
		int compressedSize,
		int dstCapacity)
	{
		printf("We have successfully infiltrated the LZ4 Decompress Safe Ext function...\n");

		const int ret = reinterpret_cast<decltype(LZ4_decompress_safe_ext_hook)*>(LZ4_decompress_safe_ext_orig)(
			src, dst, compressedSize, dstCapacity);

		const std::string data(dst, ret);

		auto notifier_thread = std::thread([&]
			{
				notifier::notify_response(data);
			});

		notifier_thread.join();

		return ret;
	}

	int LZ4_compress_default_ext_hook(
		char* src,
		char* dst,
		int srcSize,
		int dstCapacity)
	{
		printf("We have successfully infiltrated the LZ4 Compress Default Ext function...\n");
		
		const int ret = reinterpret_cast<decltype(LZ4_compress_default_ext_hook)*>(LZ4_compress_default_ext_orig)(
			src, dst, srcSize, dstCapacity);

		const std::string data(src, srcSize);

		auto notifier_thread = std::thread([&]
			{
				notifier::notify_request(data);
			});

		notifier_thread.join();

		return ret;
	}

	Il2CppString* get_DatabaseSavePath_hook() {
		if (g_savedata_path.empty())
		{
			return reinterpret_cast<decltype(get_DatabaseSavePath_hook)*>(get_DatabaseSavePath_orig)();
		}
		else
		{
			Il2CppString* ovr = il2cpp_string_new(g_savedata_path.c_str());
			wprintf(L"Override SaveDataPath to %s\n", ovr->start_char);
			return ovr;
		}
	}

	Il2CppString* GetMasterdataDirectory_hook() {
		if (g_savedata_path.empty())
		{
			return reinterpret_cast<decltype(GetMasterdataDirectory_hook)*>(GetMasterdataDirectory_orig)();
		}
		else
		{
			Il2CppString* ovr = il2cpp_string_new(g_savedata_path.c_str());
			wprintf(L"Override GetMasterdataDirectory to %s\n", ovr->start_char);
			return ovr;
		}
	}

	void SaveRawData(const char* buffer, size_t length, const std::string& prefix) {
		if (!buffer || length == 0) {
			printf("[Raw Saver] Error: Buffer is null or length is zero.\n");
			return;
		}

		if (length <= 0 || length > 20 * 1024 * 1024) {
			printf("[Raw Saver] Error: Unreliable length %zu for %s. Skipping save.\n", length, prefix.c_str());
			return;
		}


		auto now = std::chrono::system_clock::now();
		auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

		// note to self: create a "Packets" folder in umamusume directory (where the .exe is)
		std::string filename = "Packets\\" + prefix + "_" + std::to_string(timestamp) + ".bin";

		// Open the file in binary mode. This is ESSENTIAL.
		std::ofstream out_file(filename, std::ios::binary);
		if (out_file.is_open()) {
			out_file.write(buffer, length);
			out_file.flush();

			printf("[Raw Saver] Successfully saved %zu bytes to %s\n", length, filename.c_str());
			out_file.close();
		}
		else {
			printf("[Raw Saver] Error: Could not open file %s for writing.\n", filename.c_str());
		}
	}

	void SaveIl2CppArrayData(Il2CppArraySize_t<int8_t>* array_obj, const std::string& prefix) {
		if (!array_obj) {
			printf("[Array Saver] Error: Object pointer is null for %s.\n", prefix.c_str());
			return;
		}

		// --- The Breakthrough ---
		// Cast the object to an array of pointers/uintptrs to read its fields.
		uintptr_t* fields = reinterpret_cast<uintptr_t*>(array_obj);
		// The true length is at Offset 3.
		size_t true_length = static_cast<size_t>(fields[3]);

		// Sanity check the length.
		if (true_length <= 0 || true_length > 20 * 1024 * 1024) { // 20MB limit
			printf("[Array Saver] Error: Found invalid or insane length %zu for %s. Skipping save.\n", true_length, prefix.c_str());
			return;
		}

		// The actual byte data starts after the header.
		// The header is Il2CppObject (16 bytes) + bounds (8 bytes) + max_length (8 bytes) = 32 bytes.
		// This is our kIl2CppSizeOfArray.
		const char* buffer = reinterpret_cast<const char*>(array_obj) + 32; // Use a fixed, known offset.

		// --- The Standard File Write ---
		auto now = std::chrono::system_clock::now();
		auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
		std::string filename = "Packets\\" + prefix + "_" + std::to_string(timestamp) + ".bin";

		std::ofstream out_file(filename, std::ios::binary);
		if (out_file.is_open()) {
			out_file.write(buffer, true_length);
			out_file.flush();
			out_file.close();
			printf("[Array Saver] Success! Saved %zu bytes to %s\n", true_length, filename.c_str());
		}
		else {
			printf("[Array Saver] Error: Could not open file %s for writing.\n", filename.c_str());
		}
	}

	Il2CppArraySize_t<int8_t>* HttpHelper_CompressRequest_hook(Il2CppArraySize_t<int8_t>* data) {
		printf("We have successfully infiltrated the Compress Request function...\n");

		SaveIl2CppArrayData(data, "request_uncompressed");

		// Print the first 256 bytes of the buffer to see what it contains.
		//printf("[DEBUG] Hex dump of the first 256 bytes of the request buffer:\n");
		//print_hex_dump(buf, (data->max_length > 256) ? 256 : data->max_length);

		auto buf = reinterpret_cast<const char*>(data) + kIl2CppSizeOfArray;
		auto compressed_result = reinterpret_cast<decltype(HttpHelper_CompressRequest_hook)*>(HttpHelper_CompressRequest_orig)(data);

		SaveIl2CppArrayData(compressed_result, "request_compressed");

		try {
			const size_t buffer_length = data->max_length;
			
			if (buffer_length > 50 * 1024 * 1024) {
				printf("[MsgPack Saver] Warning: Request buffer is very large (%zu bytes), skipping JSON conversion\n", buffer_length);
			} else {
				//MsgPackData::DisplayMsgPackData(buf, buffer_length);
				MsgPackData::SaveMsgPackData(buf, buffer_length, "request");
			}
		}
		catch (const std::exception& e) {
			// e.what() contains the error message from the exception
			printf("[ERROR] An exception occurred while reading the response: %s\n", e.what());
		}
		catch (...) {
			printf("An error occurred while trying to read the request to be sent\n");
		}


		return compressed_result;
	}

	Il2CppArraySize_t<int8_t>* HttpHelper_DecompressResponse_hook(Il2CppArraySize_t<int8_t>* compressed) {
		printf("We have successfully infiltrated the Decompress Response function...\n");

		SaveIl2CppArrayData(compressed, "response_compressed");

		auto data = reinterpret_cast<decltype(HttpHelper_DecompressResponse_hook)*>(HttpHelper_DecompressResponse_orig)(compressed);
		auto buf = reinterpret_cast<const char*>(data) + kIl2CppSizeOfArray;

		//printf("[DEBUG] Reported data->max_length: %u\n", data->max_length);
		

		// first 256 bytes of the buffer to see what it contains
		//printf("[DEBUG] Hex dump of the first 256 bytes of the response buffer:\n");
		//print_hex_dump(buf, (data->max_length > 256) ? 256 : data->max_length);

		SaveIl2CppArrayData(data, "response_uncompressed");

		try {
			const size_t buffer_length = data->max_length;
			const int HEADER_SIZE = 0; // No 170-bytes long header? Might be because of where we retrieve the data?
			
			if (buffer_length > 50 * 1024 * 1024) {
				printf("[MsgPack Saver] Warning: Response buffer is very large (%zu bytes), skipping JSON conversion\n", buffer_length);
			} else {
				//MsgPackData::DisplayMsgPackData(buf + HEADER_SIZE, buffer_length - HEADER_SIZE);
				MsgPackData::SaveMsgPackData(buf + HEADER_SIZE, buffer_length - HEADER_SIZE, "response");
			}

		}
		catch (const std::exception& e) {
			// e.what() contains the error message from the exception
			printf("[ERROR] An exception occurred while reading the response: %s\n", e.what());
		}
		catch (...) {
			printf("An error occurred while trying to read the response received\n");
		}


		return data;
	}


	bool patch_game_assembly()
	{
		printf("Trying to patch GameAssembly.dll...\n");

		auto il2cpp_module = GetModuleHandle("GameAssembly.dll");

		// load il2cpp exported functions
		il2cpp_symbols::init(il2cpp_module);

#pragma region HOOK_MACRO
#define ADD_HOOK(_name_, _fmt_) \
	auto _name_##_offset = reinterpret_cast<void*>(_name_##_addr); \
	\
	printf(_fmt_, _name_##_offset); \
	\
	MH_CreateHook(_name_##_offset, _name_##_hook, &_name_##_orig); \
	MH_EnableHook(_name_##_offset); 
#pragma endregion
#pragma region HOOK_ADDRESSES
		auto set_fps_addr = il2cpp_resolve_icall("UnityEngine.Application::set_targetFrameRate(System.Int32)");

		auto get_DatabaseSavePath_addr = il2cpp_symbols::get_method_pointer(
			"umamusume.dll", "Gallop",
			"SaveDataManager", "get_DatabaseSavePath", 0
		);

		auto GetMasterdataDirectory_addr = il2cpp_symbols::get_method_pointer(
			"umamusume.dll", "Gallop",
			"MasterDataManager", "GetMasterdataDirectory", 0
		);

		auto LZ4_compress_default_ext_addr = il2cpp_symbols::get_method_pointer(
			"LibNative.Runtime.dll", "LibNative.LZ4",
			"Plugin", "LZ4_compress_default_ext", 4
		);
		
		auto LZ4_decompress_safe_ext_addr = il2cpp_symbols::get_method_pointer(
			"LibNative.Runtime.dll", "LibNative.LZ4",
			"Plugin", "LZ4_decompress_safe_ext", 4
		);
		const auto libnative = GetModuleHandle("libnative.dll");
		//LZ4_compress_default_ext_addr = GetProcAddress(libnative, "LZ4_compress_default_ext");
		//LZ4_decompress_safe_ext_addr = GetProcAddress(libnative, "LZ4_decompress_safe_ext");
		//if (LZ4_compress_default_ext_addr == nullptr || LZ4_decompress_safe_ext_addr == nullptr)
		//	return false;

#if defined _DEBUG
		auto HttpHelper_CompressRequest_addr = il2cpp_symbols::get_method_pointer(
			"umamusume.dll", "Gallop",
			"HttpHelper", "CompressRequest", 1
		);

		auto HttpHelper_DecompressResponse_addr = il2cpp_symbols::get_method_pointer(
			"umamusume.dll", "Gallop",
			"HttpHelper", "DecompressResponse", 1
		);
		ADD_HOOK(HttpHelper_DecompressResponse, "Gallop.HttpHelper::DecompressResponse at %p\n");
		ADD_HOOK(HttpHelper_CompressRequest, "Gallop.HttpHelper::CompressRequest at %p\n");
#endif

#pragma endregion
		if (g_max_fps != -1) {
			ADD_HOOK(set_fps, "UnityEngine.Application.set_targetFrameRate at %p \n");
		}
		if (!g_savedata_path.empty()) {

#if defined _DEBUG
			ADD_HOOK(get_DatabaseSavePath, "get_DatabaseSavePath at %p\n");
			ADD_HOOK(GetMasterdataDirectory, "GetMasterdataDirectory at %p\n");
#endif
		}
		ADD_HOOK(LZ4_decompress_safe_ext, "LibNative.LZ4.Plugin.LZ4_decompress_safe_ext at %p \n");
		ADD_HOOK(LZ4_compress_default_ext, "LibNative.LZ4.Plugin.LZ4_compress_default_ext at %p \n");
		return true;
	}
}

bool init_hook()
{
	if (compatible_mode) {
		std::this_thread::sleep_for(std::chrono::seconds(10));
		patch_game_assembly();
	}
	else {
		MH_CreateHook(LoadLibraryW, load_library_w_hook, &load_library_w_orig);
		MH_EnableHook(LoadLibraryW);
	}

	std::thread ping_thread([]() {
		notifier::ping();
		});
	ping_thread.detach();

	return true;
}

void uninit_hook()
{
	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}
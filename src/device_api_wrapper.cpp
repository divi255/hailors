#include "device_api_wrapper.hpp"
#include <vector>
#include <thread>
#include <iostream>

using namespace hailort;

extern "C" hailo_status hailors_create_vdevice(hailo_vdevice_handle* vdevice) {
    auto vdevice_result = VDevice::create();
    if (!vdevice_result) {
        return vdevice_result.status();
    }
    *vdevice = vdevice_result.value().release();  // Store as raw pointer
    return HAILO_SUCCESS;
}

extern "C" hailo_status hailors_release_vdevice(hailo_vdevice_handle vdevice) {
    delete static_cast<VDevice*>(vdevice);
    return HAILO_SUCCESS;
}

extern "C" hailo_status hailors_configure_hef(
    hailo_vdevice_handle vdevice,
    void *model, // Pointer to the model data
    size_t model_size, // Size of the model data
    hailo_network_group_handle* network_group,
    void ***input_vstreams,   // Pointer to an array of input vstreams
    size_t *input_count,      // Pointer to the number of input vstreams
    void ***output_vstreams,  // Pointer to an array of output vstreams
    size_t *output_count,     // Pointer to the number of output vstreams
    size_t* input_frame_size,   // New parameter for input frame size
    size_t* output_frame_size  // New parameter for output frame size
) {
    // Initialize vstreams as empty
    *input_count = 0;
    *output_count = 0;
    auto vdevice_ptr = static_cast<VDevice*>(vdevice);
    auto model_memory_view = MemoryView(model, model_size);
    auto hef_result = Hef::create(model_memory_view);
    if (!hef_result) {
        return hef_result.status();
    }
    auto hef = std::move(hef_result.value());  // Move instead of copy

    auto configure_params = vdevice_ptr->create_configure_params(hef);
    if (!configure_params) {
        return configure_params.status();
    }

    auto network_groups_result = vdevice_ptr->configure(hef, configure_params.value());
    if (!network_groups_result || network_groups_result->empty()) {
        return network_groups_result.status();
    }

    // Access the first network group
    auto configured_network_group = network_groups_result.value()[0];
    if (!configured_network_group) {
        std::cerr << "Failed to get network group from vector." << std::endl;
        return HAILO_INVALID_OPERATION;
    }

    // Create input vstreams
    auto input_vstream_params = configured_network_group->make_input_vstream_params(false, HAILO_FORMAT_TYPE_AUTO, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, "");
    if (!input_vstream_params) {
        return input_vstream_params.status();
    }
    auto input_streams_result = VStreamsBuilder::create_input_vstreams(*configured_network_group, input_vstream_params.value());
    if (!input_streams_result) {
        return input_streams_result.status();
    }
    auto input_streams = std::move(input_streams_result.value());
    *input_count = input_streams.size();

    // Populate input vstreams
    *input_vstreams = static_cast<void**>(malloc(input_streams.size() * sizeof(void*)));
    for (size_t i = 0; i < input_streams.size(); i++) {
        (*input_vstreams)[i] = new InputVStream(std::move(input_streams[i]));
    }
    *input_frame_size = input_streams.empty() ? 0 : static_cast<InputVStream*>((*input_vstreams)[0])->get_frame_size();
    *input_count = input_streams.size();

    // Create output vstreams
    auto output_vstream_params = configured_network_group->make_output_vstream_params(false, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, "");
    if (!output_vstream_params) {
        return output_vstream_params.status();
    }
    auto output_streams_result = VStreamsBuilder::create_output_vstreams(*configured_network_group, output_vstream_params.value());
    if (!output_streams_result) {
        return output_streams_result.status();
    }
    auto output_streams = std::move(output_streams_result.value());
    *output_count = output_streams.size();

    // Populate output vstreams
    *output_vstreams = static_cast<void**>(malloc(output_streams.size() * sizeof(void*)));
    for (size_t i = 0; i < output_streams.size(); i++) {
        (*output_vstreams)[i] = new OutputVStream(std::move(output_streams[i]));
    }
    *output_frame_size = output_streams.empty() ? 0 : static_cast<OutputVStream*>((*output_vstreams)[0])->get_frame_size();
    *output_count = output_streams.size();



    // Set the network group handle
    *network_group = configured_network_group.get();

    return HAILO_SUCCESS;
}


extern "C" hailo_status hailors_infer(hailo_network_group_handle network_group, void **input_vstreams, size_t input_count, void **output_vstreams, size_t output_count)
{
    if (!network_group || !input_vstreams || !output_vstreams) {
        return HAILO_INVALID_ARGUMENT;
    }

    hailo_status status = HAILO_SUCCESS;  // Default to success

    // Create input threads
    std::vector<std::thread> input_threads;
    for (size_t i = 0; i < input_count; ++i) {
        input_threads.emplace_back([&status, input_vstreams, i]() {
            auto *input_stream = static_cast<InputVStream*>(input_vstreams[i]);
            std::vector<uint8_t> data(input_stream->get_frame_size(), 0);
            for (size_t j = 0; j < 100; ++j) {
                hailo_status write_status = input_stream->write(MemoryView(data.data(), data.size()));
                if (write_status != HAILO_SUCCESS) {
                    status = write_status;  // Return the actual error status
                    return;
                }
            }
            hailo_status flush_status = input_stream->flush();
            if (flush_status != HAILO_SUCCESS) {
                status = flush_status;  // Return the flush failure status if applicable
            }
        });
    }

    // Create output threads
    std::vector<std::thread> output_threads;
    for (size_t i = 0; i < output_count; ++i) {
        output_threads.emplace_back([&status, output_vstreams, i]() {
            auto *output_stream = static_cast<OutputVStream*>(output_vstreams[i]);
            std::vector<uint8_t> data(output_stream->get_frame_size());
            for (size_t j = 0; j < 100; ++j) {
                hailo_status read_status = output_stream->read(MemoryView(data.data(), data.size()));
                if (read_status != HAILO_SUCCESS) {
                    status = read_status;  // Return the actual read error status
                    return;
                }
            }
        });
    }

    // Join all threads
    for (auto &t : input_threads) {
        t.join();
    }
    for (auto &t : output_threads) {
        t.join();
    }

    if (status == HAILO_SUCCESS) {
        std::cout << "Inference completed successfully." << std::endl;
    } else {
        std::cerr << "Inference failed with status: " << status << std::endl;
    }

    return status;
}

extern "C" hailo_status hailors_write_input_frame(
    hailo_input_vstream_handle input_vstream,
    const void* data,
    size_t data_size
) {
    if (!input_vstream || !data) {
        std::cerr << "Invalid input stream handle or data buffer." << std::endl;
        return HAILO_INVALID_ARGUMENT;
    }

    // Cast the handle to InputVStream
    auto vstream = static_cast<hailort::InputVStream*>(input_vstream);

    // Create a MemoryView for the input data (casting to `void*`)
    hailort::MemoryView input_view(const_cast<void*>(data), data_size);

    // Write data using the MemoryView
    auto status = vstream->write(input_view);
    if (status != HAILO_SUCCESS) {
        std::cerr << "Failed to write data to input vstream. Status: " << status << std::endl;
    }

    return status;
}

extern "C" hailo_status hailors_read_output_frame(
    void* output_vstream, 
    void* buffer,
    size_t buffer_size
) {
    if (!output_vstream) {
        std::cerr << "output_vstream is null." << std::endl;
        return HAILO_INVALID_ARGUMENT;
    }

    if (!buffer || buffer_size == 0) {
        std::cerr << "Buffer is null or has invalid size." << std::endl;
        return HAILO_INVALID_ARGUMENT;
    }


        // Cast the handle to OutputVStream
    auto vstream = static_cast<hailort::OutputVStream*>(output_vstream);

    // Create a MemoryView for the output buffer
    hailort::MemoryView output_view(buffer, buffer_size);
    if (!vstream) {
        std::cerr << "Invalid output_vstream (null pointer)." << std::endl;
        return HAILO_INVALID_ARGUMENT;
    }

    // Read data using the MemoryView
    auto status = vstream->read(output_view);


    if (status != HAILO_SUCCESS) {
        std::cerr << "Failed to read data from output vstream. Status: " << status << std::endl;
    }

    return status;
}

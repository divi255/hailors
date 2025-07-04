#ifndef DEVICE_API_WRAPPER_HPP
#define DEVICE_API_WRAPPER_HPP

#include "hailo/hailort.hpp"


extern "C" {



// Handle typedefs
typedef void* hailo_vdevice_handle;
typedef void* hailo_network_group_handle;
typedef void* hailo_input_vstream_handle;
typedef void* hailo_output_vstream_handle;

// Function declarations
hailo_status hailors_create_vdevice(hailo_vdevice_handle* vdevice);
hailo_status hailors_release_vdevice(hailo_vdevice_handle vdevice);

hailo_status hailors_configure_hef(
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
);

hailo_status hailors_infer(
    hailo_network_group_handle network_group,
    hailo_input_vstream_handle* input_vstreams,
    size_t input_count,
    hailo_output_vstream_handle* output_vstreams,
    size_t output_count);

}

extern "C" hailo_status hailors_write_input_frame(
    hailo_input_vstream_handle input_vstream,
    const void* data,
    size_t data_size
);

extern "C" hailo_status hailors_read_output_frame(
    void* output_vstream, 
    void* buffer,
    size_t buffer_size
);

#endif // DEVICE_API_WRAPPER_HPP

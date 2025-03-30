/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model.hpp
 * @brief Async Infer
 **/

#ifndef _HAILO_ASYNC_INFER_HPP_
#define _HAILO_ASYNC_INFER_HPP_

#include "hailo/network_group.hpp"
#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"

/** hailort namespace */
namespace hailort
{

class AsyncInferJobBase;
class ConfiguredInferModelBase;
class AsyncInferRunnerImpl;

/*! Asynchronous inference job representation is used to manage and control an inference job that is running asynchronously. */
class HAILORTAPI AsyncInferJob
{
public:
    AsyncInferJob() : m_should_wait_in_dtor(false) {};
    virtual ~AsyncInferJob();

    AsyncInferJob(const AsyncInferJob &other) = delete;
    AsyncInferJob &operator=(const AsyncInferJob &other) = delete;
    AsyncInferJob(AsyncInferJob &&other);
    AsyncInferJob &operator=(AsyncInferJob &&other);

    /**
     * Waits for the asynchronous inference job to finish.
     *
     * @param[in] timeout The maximum time to wait.
     *
     * @return A ::hailo_status indicating the status of the operation.
     *  If the job finishes successfully within the timeout, ::HAILO_SUCCESS is returned. Otherwise, returns a ::hailo_status error
     **/
    hailo_status wait(std::chrono::milliseconds timeout);

    /**
     * Detaches the job. Without detaching, the job's destructor will block until the job finishes.
     **/
    void detach();

private:
    friend class AsyncInferJobBase;

    AsyncInferJob(std::shared_ptr<AsyncInferJobBase> pimpl);
    std::shared_ptr<AsyncInferJobBase> m_pimpl;
    bool m_should_wait_in_dtor;
};

struct AsyncInferCompletionInfo;

static const auto ASYNC_INFER_EMPTY_CALLBACK = [](const AsyncInferCompletionInfo&) {};

/*! Configured infer_model that can be used to perform an asynchronous inference */
class HAILORTAPI ConfiguredInferModel
{
public:
    ConfiguredInferModel() = default;

    /** Represents an asynchronous infer request - holds the input and output buffers of the request */
    class HAILORTAPI Bindings
    {
    public:
        Bindings() = default;
        Bindings(const Bindings &other);
        Bindings &operator=(const Bindings &other);

        /** Holds the input and output buffers of the Bindings infer request */
        class HAILORTAPI InferStream
        {
        public:
            /**
             * Sets the edge's buffer to a new one, of type MemoryView.
             *
             * For best performance and to avoid memory copies, buffers should be aligned to the system PAGE_SIZE.
             *
             * On output streams, the actual buffer allocated size must be aligned to PAGE_SIZE as well - otherwise some
             * memory corruption might occur at the end of the last page. For example, if the buffer size is 4000
             * bytes, the actual buffer size should be at least 4096 bytes. To fill all requirements, it is recommended
             * to allocate the buffer with standard page allocation function provided by the os (mmap on linux,
             * VirtualAlloc in windows).
             *
             * @param[in] view      The new buffer to be set.
             * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
             */
            hailo_status set_buffer(MemoryView view);

            /**
            * @return Upon success, returns Expected of the MemoryView buffer of the edge.
            * Otherwise, returns Unexpected of ::hailo_status error.
            * @note If buffer type is not MemoryView, will return ::HAILO_INVALID_OPERATION.
            */
            Expected<MemoryView> get_buffer() const;

            /**
             * Sets the edge's buffer to a new one, of type hailo_pix_buffer_t.
             *
             * Each plane in the \ref hailo_pix_buffer_t must feet the requirements listed in \ref set_buffer.
             *
             * @param[in] pix_buffer      The new buffer to be set.
             * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
             * @note Supported only for inputs.
             * @note Currently only support memory_type field of buffer to be HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR.
             */
            hailo_status set_pix_buffer(const hailo_pix_buffer_t &pix_buffer);

            /**
             * @return Upon success, returns Expected of the ::hailo_pix_buffer_t buffer of the edge.
            * Otherwise, returns Unexpected of ::hailo_status error.
            * @note If buffer type is not ::hailo_pix_buffer_t, will return ::HAILO_INVALID_OPERATION.
            */
            Expected<hailo_pix_buffer_t> get_pix_buffer() const;

            /**
             * Sets the edge's buffer from a DMA buffer.
             *
             * @param[in] dma_buffer      The new buffer to be set.
             * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
             * @note Supported on Linux only.
             */
            hailo_status set_dma_buffer(hailo_dma_buffer_t dma_buffer);

            /**
             * @return Upon success, returns Expected of the ::hailo_dma_buffer_t buffer of the edge.
            * Otherwise, returns Unexpected of ::hailo_status error.
            * @note If buffer type is not ::hailo_dma_buffer_t, will return ::HAILO_INVALID_OPERATION.
            * @note Supported on Linux only.
            */
            Expected<hailo_dma_buffer_t> get_dma_buffer() const;

        private:
            friend class ConfiguredInferModelBase;
            friend class AsyncInferRunnerImpl;
            friend class Bindings;

            Expected<InferStream> inner_copy() const;

            class Impl;
            InferStream(std::shared_ptr<Impl> pimpl);
            std::shared_ptr<Impl> m_pimpl;
        };

        /**
         * Returns the single input's InferStream object.
         *
         * @return Upon success, returns Expected of the single input's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
         * @note If Bindings has multiple inputs, will return ::HAILO_INVALID_OPERATION.
         *  In that case - use input(const std::string &name) instead.
         */
        Expected<InferStream> input();

        /**
         * Returns the single output's InferStream object.
         *
         * @return Upon success, returns Expected of the single output's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
         * @note If Bindings has multiple outputs, will return ::HAILO_INVALID_OPERATION.
         *  In that case - use output(const std::string &name) instead.
         */
        Expected<InferStream> output();

        /**
         * Gets an input's InferStream object.
         *
         * @param[in] name                    The name of the input edge.
         * @return Upon success, returns Expected of the relevant InferStream object. Otherwise, returns a ::hailo_status error.
         */
        Expected<InferStream> input(const std::string &name);

        /**
         * Gets an output's InferStream object.
         *
         * @param[in] name                    The name of the output edge.
         * @return Upon success, returns Expected of the relevant InferStream object. Otherwise, returns a ::hailo_status error.
         */
        Expected<InferStream> output(const std::string &name);

        /**
         * Returns the single input's InferStream object, as readonly.
         *
         * @return Upon success, returns Expected of the single input's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
         * @note If Bindings has multiple inputs, will return ::HAILO_INVALID_OPERATION.
         *  In that case - use input(const std::string &name) instead.
         */
        Expected<InferStream> input() const;

        /**
         * Returns the single output's InferStream object, as readonly.
         *
         * @return Upon success, returns Expected of the single output's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
         * @note If Bindings has multiple outputs, will return ::HAILO_INVALID_OPERATION.
         *  In that case - use output(const std::string &name) instead.
         */
        Expected<InferStream> output() const;

        /**
         * Gets an input's InferStream object, as readonly.
         *
         * @param[in] name                    The name of the input edge.
         * @return Upon success, returns Expected of the relevant InferStream object. Otherwise, returns a ::hailo_status error.
         */
        Expected<InferStream> input(const std::string &name) const;

        /**
         * Gets an output's InferStream object, as readonly.
         *
         * @param[in] name                    The name of the output edge.
         * @return Upon success, returns Expected of the relevant InferStream object. Otherwise, returns a ::hailo_status error.
         */
        Expected<InferStream> output(const std::string &name) const;

    private:
        friend class ConfiguredInferModelBase;

        void init_bindings_from(const Bindings &other);

        Bindings(std::unordered_map<std::string, InferStream> &&inputs,
            std::unordered_map<std::string, InferStream> &&outputs);

        std::unordered_map<std::string, Bindings::InferStream> m_inputs;
        std::unordered_map<std::string, Bindings::InferStream> m_outputs;
    };

    /**
     * Creates a Bindings object.
     *
     * @return Upon success, returns Expected of Bindings. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<Bindings> create_bindings();

    /**
     * Creates a Bindings object.
     *
     * @param[in] buffers           map of input and output names and buffers.
     *
     * @return Upon success, returns Expected of Bindings. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<Bindings> create_bindings(const std::map<std::string, MemoryView> &buffers);

    /**
     * The readiness of the model to launch is determined by the ability to push buffers to the asynchronous inference pipeline.
     * If the model is ready, the method will return immediately.
     * If the model is not ready, the method will wait for the model to be ready.
     *
     * @param[in] timeout           Amount of time to wait until the model is ready in milliseconds.
     * @param[in] frames_count      The count of buffers you intent to infer in the next request. Useful for batch inference.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If @a timeout has passed and the model is not ready, returns ::HAILO_TIMEOUT.
     *           - In any other error case, returns ::hailo_status error.
     *
     * @note Calling this function with frames_count greater than get_async_queue_size() will timeout.
     */
    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count = 1);

    /**
     * Activates hailo device inner-resources for inference.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns ::hailo_status error.
     * @note Calling this function is invalid in case scheduler is enabled.
     */
    hailo_status activate();

    /**
     * Deactivates hailo device inner-resources for inference.
     * @return Returns ::HAILO_SUCCESS.
     * @note Calling this function is invalid in case scheduler is enabled.
     */
    hailo_status deactivate();

    /**
     * Launches a synchronous inference operation with the provided bindings.
     *
     * @param[in] bindings           The bindings for the inputs and outputs of the model.
     * @param[in] timeout            The maximum amount of time to wait for the inference operation to complete.
     *
     * @return Upon success, returns ::HAILO_SUCCESS.
     *  Otherwise, returns Unexpected of ::hailo_status error.
     */
    hailo_status run(const Bindings &bindings, std::chrono::milliseconds timeout);

    /**
     * Launches an asynchronous inference operation with the provided bindings.
     * The completion of the operation is notified through the provided callback function.
     *
     * @param[in] bindings           The bindings for the inputs and outputs of the model.
     * @param[in] callback           The function to be called upon completion of the asynchronous inference operation.
     *
     * @return Upon success, returns an instance of Expected<AsyncInferJob> representing the launched job.
     *  Otherwise, returns Unexpected of ::hailo_status error, and the interface shuts down completly.
     * @note @a callback should execute as quickly as possible.
     * @note The bindings' buffers should be kept intact until the async job is completed.
     * @note To ensure the inference pipeline can handle new buffers, it is recommended to first call \ref wait_for_async_ready
     */
    Expected<AsyncInferJob> run_async(const Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback = ASYNC_INFER_EMPTY_CALLBACK);

    /**
     * Launches an asynchronous inference operation with the provided bindings.
     * The completion of the operation is notified through the provided callback function.
     * Overload for multiple-bindings inference (useful for batch inference).
     *
     * @param[in] bindings           The bindings for the inputs and outputs of the model.
     * @param[in] callback           The function to be called upon completion of the asynchronous inference operation.
     *
     * @return Upon success, returns an instance of Expected<AsyncInferJob> representing the launched job.
     *  Otherwise, returns Unexpected of ::hailo_status error, and the interface shuts down completly.
     * @note @a callback should execute as quickly as possible.
     * @note The bindings' buffers should be kept intact until the async job is completed.
     * @note To ensure the inference pipeline can handle new buffers, it is recommended to first call \ref wait_for_async_ready
     */
    Expected<AsyncInferJob> run_async(const std::vector<Bindings> &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback = ASYNC_INFER_EMPTY_CALLBACK);

    /**
    * @return Upon success, returns Expected of LatencyMeasurementResult object containing the output latency result.
    *  Otherwise, returns Unexpected of ::hailo_status error.
    */
    Expected<LatencyMeasurementResult> get_hw_latency_measurement();

    /**
     * Sets the maximum time period that may pass before receiving run time from the scheduler.
     * This will occur providing at least one send request has been sent, there is no minimum requirement for send
     *  requests, (e.g. threshold - see set_scheduler_threshold()).
     *
     * @param[in]  timeout              Timeout in milliseconds.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note The new time period will be measured after the previous time the scheduler allocated run time to this network group.
     * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
     * @note The default timeout is 0ms.
     */
    hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout);

    /**
     * Sets the minimum number of send requests required before the network is considered ready to get run time from the scheduler.
     *
     * @param[in]  threshold            Threshold in number of frames.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
     * @note The default threshold is 1.
     * @note If at least one send request has been sent, but the threshold is not reached within a set time period (e.g. timeout - see
     *  hailo_set_scheduler_timeout()), the scheduler will consider the network ready regardless.
     */
    hailo_status set_scheduler_threshold(uint32_t threshold);

    /**
     * Sets the priority of the network.
     * When the network group scheduler will choose the next network, networks with higher priority will be prioritized in the selection.
     * bigger number represent higher priority.
     *
     * @param[in]  priority             Priority as a number between HAILO_SCHEDULER_PRIORITY_MIN - HAILO_SCHEDULER_PRIORITY_MAX.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
     * @note The default priority is HAILO_SCHEDULER_PRIORITY_NORMAL.
     */
    hailo_status set_scheduler_priority(uint8_t priority);

    /**
     * @return Upon success, returns Expected of a the number of inferences that can be queued simultaneously for execution.
     *  Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<size_t> get_async_queue_size() const;

    /**
     * Shuts the inference down. After calling this method, the model is no longer usable.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error
     */
    hailo_status shutdown();


    hailo_status update_cache_offset(int32_t offset_delta_entries);

private:
    friend class InferModelBase;
    friend class ConfiguredInferModelBase;

    ConfiguredInferModel(std::shared_ptr<ConfiguredInferModelBase> pimpl);

    std::shared_ptr<ConfiguredInferModelBase> m_pimpl;
};

/** Context passed to the callback function after the asynchronous inference operation was completed or has failed. */
struct HAILORTAPI AsyncInferCompletionInfo
{
    /**
     * Constructor for AsyncInferCompletionInfo.
     *
     * @param[in] _status The status of the inference operation.
     */
    AsyncInferCompletionInfo(hailo_status _status) : status(_status)
    {
    }

    /**
     * Status of the asynchronous inference operation.
     * - ::HAILO_SUCCESS - When the inference operation is complete successfully.
     * - Any other ::hailo_status on unexpected errors.
     */
    hailo_status status;
};

/**
 * Contains all of the necessary information for configuring the network for inference.
 * This class is used to set up the model for inference and includes methods for setting and getting the model's parameters.
 * By calling the configure function, the user can create a ConfiguredInferModel object, which is used to run inference.
 */
class HAILORTAPI InferModel
{
public:
    virtual ~InferModel() = default;

    /**
     * Represents the parameters of a stream.
     * In default, the stream's parameters are set to the default values of the model.
     * The user can change the stream's parameters by calling the set_ functions.
     */
    class HAILORTAPI InferStream
    {
    public:
        /**
         * @return The name of the stream.
         */
        const std::string name() const;

        /**
         * @return The shape of the image that the stream will use for inference.
         */
        hailo_3d_image_shape_t shape() const;

        /**
         * @return The format that the stream will use for inference.
         */
        hailo_format_t format() const;

        /**
         * @return The size in bytes of a frame that the stream will use for inference.
         */
        size_t get_frame_size() const;

        /**
         * @return upon success, an Expected of hailo_nms_shape_t, the NMS shape for the stream.
         *  Otherwise, returns Unexpected of ::hailo_status error.
         * @note In case NMS is disabled, returns an unexpected of ::HAILO_INVALID_OPERATION.
         */
        Expected<hailo_nms_shape_t> get_nms_shape() const;

        /**
         * Sets the format type of the stream.
         * This method is used to specify the format type that the stream will use for inference.
         *
         * @param[in] type The format type to be set for the stream. This should be a value of the hailo_format_type_t enum.
         */
        void set_format_type(hailo_format_type_t type);

        /**
         * Sets the format order of the stream.
         * This method is used to specify the format order that the stream will use for inference.
         *
         * @param[in] order The format order to be set for the stream. This should be a value of the hailo_format_order_t enum.
         */
        void set_format_order(hailo_format_order_t order);

        /**
         * Retrieves the quantization information for all layers in the model.
         * @return A vector of hailo_quant_info_t structures, each representing the quantization information for a layer in the model.
         */
        std::vector<hailo_quant_info_t> get_quant_infos() const;

        /**
         * Checks if Non-Maximum Suppression (NMS) is enabled for the model.
         *
         * @return True if NMS is enabled, false otherwise.
         */
        bool is_nms() const;

        /**
         * Set NMS score threshold, used for filtering out candidates. Any box with score<TH is suppressed.
         *
         * @param[in] threshold        NMS score threshold to set.
         */
        void set_nms_score_threshold(float32_t threshold);

        /**
         * Set NMS intersection over union overlap Threshold,
         * used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.
         *
         * @param[in] threshold        NMS IoU threshold to set.
         */
        void set_nms_iou_threshold(float32_t threshold);

        /**
         * Set a limit for the maximum number of boxes per class.
         *
         * @param[in] max_proposals_per_class NMS max proposals per class to set.
         */
        void set_nms_max_proposals_per_class(uint32_t max_proposals_per_class);

        /**
         * Set a limit for the maximum number of boxes total (all classes).
         *
         * @param[in] max_proposals_total NMS max proposals total to set.
         */
        void set_nms_max_proposals_total(uint32_t max_proposals_total);

        /**
         * Set maximum accumulated mask size for all the detections in a frame.
         *
         * @note: Used in order to change the output buffer frame size
         * in cases where the output buffer is too small for all the segmentation detections.
         *
         * @param[in] max_accumulated_mask_size NMS max accumulated mask size.
         */
        void set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size);

    private:
        friend class InferModelBase;
        friend class InferModelHrpcClient;

        float32_t nms_score_threshold() const;
        float32_t nms_iou_threshold() const;
        uint32_t nms_max_proposals_per_class() const;
        uint32_t nms_max_proposals_total() const;
        uint32_t nms_max_accumulated_mask_size() const;

        class Impl;
        InferStream(std::shared_ptr<Impl> pimpl);

        std::shared_ptr<Impl> m_pimpl;
    };

    /**
     * @return A constant reference to the Hef object associated with this InferModel.
     */
    virtual const Hef &hef() const = 0;

    /**
     * Sets the batch size of the InferModel.
     * This parameter determines the number of frames that be sent for inference in a single batch.
     * If a scheduler is enabled, this parameter determines the 'burst size' - the max number of frames after which the scheduler will attempt
     *  to switch to another model.
     * If scheduler is disabled, the number of frames for inference should be a multiplication of batch_size (unless model is in single context).
     *
     * @note The default value is @a HAILO_DEFAULT_BATCH_SIZE - which means the batch is determined by HailoRT automatically.
     *
     * @param[in] batch_size      The new batch size to be set.
     */
    virtual void set_batch_size(uint16_t batch_size) = 0;

    /**
     * Sets the power mode of the InferModel.
     * See ::hailo_power_mode_t for more information.
     *
     * @param[in] power_mode      The new power mode to be set.
     */
    virtual void set_power_mode(hailo_power_mode_t power_mode) = 0;

    /**
     * Sets the latency measurement flags of the InferModel.
     * see ::hailo_latency_measurement_flags_t for more information.
     *
     * @param[in] latency      The new latency measurement flags to be set.
     */
    virtual void set_hw_latency_measurement_flags(hailo_latency_measurement_flags_t latency) = 0;

    /**
     * Configures the InferModel object. Also checks the validity of the configuration's formats.
     *
     * @return Upon success, returns Expected of ConfiguredInferModel, which can be used to perform an asynchronous inference.
     *  Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<ConfiguredInferModel> configure() = 0;

    /**
     * Returns the single input's InferStream object.
     *
     * @return Upon success, returns Expected of the single input's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
     * @note If InferModel has multiple inputs, will return ::HAILO_INVALID_OPERATION.
     *  In that case - use input(const std::string &name) instead.
     */
    virtual Expected<InferStream> input() = 0;

    /**
     * Returns the single output's InferStream object.
     *
     * @return Upon success, returns Expected of the single output's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
     * @note If InferModel has multiple outputs, will return ::HAILO_INVALID_OPERATION.
     *  In that case - use output(const std::string &name) instead.
     */
    virtual Expected<InferStream> output() = 0;

    /**
     * Gets an input's InferStream object.
     *
     * @param[in] name                    The name of the input edge.
     * @return Upon success, returns Expected of the relevant InferStream object. Otherwise, returns a ::hailo_status error.
     */
    virtual Expected<InferStream> input(const std::string &name) = 0;

    /**
     * Gets an output's InferStream object.
     *
     * @param[in] name                    The name of the output edge.
     * @return Upon success, returns Expected of the relevant InferStream object. Otherwise, returns a ::hailo_status error.
     */
    virtual Expected<InferStream> output(const std::string &name) = 0;

    /**
     * @return A constant reference to the vector of input InferStream objects, each representing an input edge.
     */
    virtual const std::vector<InferStream> &inputs() const = 0;

    /**
     * @return A constant reference to the vector of output InferStream objects, each representing an output edge.
     */
    virtual const std::vector<InferStream> &outputs() const = 0;

    /**
     * @return A constant reference to a vector of strings, each representing the name of an input stream.
     */
    virtual const std::vector<std::string> &get_input_names() const = 0;

    /**
     * @return A constant reference to a vector of strings, each representing the name of an output stream.
     */
    virtual const std::vector<std::string> &get_output_names() const = 0;

    virtual Expected<ConfiguredInferModel> configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes = {},
        const std::unordered_map<std::string, size_t> outputs_frame_sizes = {},
        std::shared_ptr<ConfiguredNetworkGroup> net_group = nullptr) = 0;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_INFER_HPP_ */

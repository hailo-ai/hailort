/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
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

#include <condition_variable>
#include <mutex>

/** hailort namespace */
namespace hailort
{

class ConfiguredInferModelImpl;
class AsyncInferRunnerImpl;

/*! Asynchronous inference job representation is used to manage and control an inference job that is running asynchronously. */
class HAILORTAPI AsyncInferJob
{
public:
    AsyncInferJob() = default;
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
    friend class ConfiguredInferModelImpl;

    class Impl;
    AsyncInferJob(std::shared_ptr<Impl> pimpl);
    std::shared_ptr<Impl> m_pimpl;
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

        /** Holds the input and output buffers of the Bindings infer request */
        class HAILORTAPI InferStream
        {
        public:
            /**
             * Sets the edge's buffer to a new one, of type MemoryView.
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
            Expected<MemoryView> get_buffer();

            /**
             * Sets the edge's buffer to a new one, of type hailo_pix_buffer_t.
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
            Expected<hailo_pix_buffer_t> get_pix_buffer();

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
            Expected<hailo_dma_buffer_t> get_dma_buffer();

        private:
            friend class ConfiguredInferModelImpl;
            friend class AsyncInferRunnerImpl;

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

    private:
        friend class ConfiguredInferModelImpl;

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
     * Waits until the model is ready to launch a new asynchronous inference operation.
     * The readiness of the model is determined by the ability to push buffers to the asynchronous inference pipeline.
     *
     * @param[in] timeout           Amount of time to wait until the model is ready in milliseconds.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If @a timeout has passed and the model is not ready, returns ::HAILO_TIMEOUT.
     *           - In any other error case, returns ::hailo_status error.
     */
    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout);

    /**
     * Activates hailo device inner-resources for context_switch inference.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns ::hailo_status error.
     * @note Calling this function is invalid in case scheduler is enabled.
     */
    hailo_status activate();

    /**
     * Deactivates hailo device inner-resources for context_switch inference.
     * @note Calling this function is invalid in case scheduler is enabled.
     */
    void deactivate();

    /**
     * Launches a synchronous inference operation with the provided bindings.
     *
     * @param[in] bindings           The bindings for the inputs and outputs of the model.
     * @param[in] timeout            The maximum amount of time to wait for the inference operation to complete.
     *
     * @return Upon success, returns ::HAILO_SUCCESS.
     *  Otherwise, returns Unexpected of ::hailo_status error.
     */
    hailo_status run(Bindings bindings, std::chrono::milliseconds timeout);

    /**
     * Launches an asynchronous inference operation with the provided bindings.
     * The completion of the operation is notified through the provided callback function.
     *
     * @param[in] bindings           The bindings for the inputs and outputs of the model.
     * @param[in] callback           The function to be called upon completion of the asynchronous inference operation.
     *
     * @return Upon success, returns an instance of Expected<AsyncInferJob> representing the launched job.
     *  Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<AsyncInferJob> run_async(Bindings bindings,
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
    Expected<size_t> get_async_queue_size();

    /**
     * Shuts the inference down. After calling this method, the model is no longer usable.
     */
    void shutdown();

private:
    friend class InferModel;

    ConfiguredInferModel(std::shared_ptr<ConfiguredInferModelImpl> pimpl);

    std::shared_ptr<ConfiguredInferModelImpl> m_pimpl;
};

/**
 * Context passed to the callback function after the asynchronous inference operation was completed or has failed.
 */
struct HAILORTAPI AsyncInferCompletionInfo
{
    /**
     * Constructor for AsyncInferCompletionInfo.
     *
     * @param[in] _bindings The bindings used for the inference operation.
     * @param[in] _status The status of the inference operation.
     */
    AsyncInferCompletionInfo(ConfiguredInferModel::Bindings _bindings, hailo_status _status) : bindings(_bindings), status(_status)
    {
    }

    /**
     * The bindings used for the inference operation.
     * This includes the input and output buffers of the request.
     */
    ConfiguredInferModel::Bindings bindings;

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
class HAILORTAPI InferModel final
{
public:
    ~InferModel() = default;

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
         * Set maximum accumulated mask size for all the detections in a frame.
         *
         * Note: Used in order to change the output buffer frame size,
         * in cases where the output buffer is too small for all the segmentation detections.
         *
         * @param[in] max_accumulated_mask_size NMS max accumulated mask size.
         */
        void set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size);

    private:
        friend class InferModel;
        friend class VDevice;

        class Impl;
        InferStream(std::shared_ptr<Impl> pimpl);

        std::shared_ptr<Impl> m_pimpl;
    };

    /**
     * @return A constant reference to the Hef object associated with this InferModel.
     */
    const Hef &hef() const;

    /**
     * Sets the batch size of the InferModel.
     *
     * @param[in] batch_size      The new batch size to be set.
     */
    void set_batch_size(uint16_t batch_size);

    /**
     * Sets the power mode of the InferModel.
     * See ::hailo_power_mode_t for more information.
     *
     * @param[in] power_mode      The new power mode to be set.
     */
    void set_power_mode(hailo_power_mode_t power_mode);

    /**
     * Sets the latency measurement flags of the InferModel.
     * see ::hailo_latency_measurement_flags_t for more information.
     *
     * @param[in] latency      The new latency measurement flags to be set.
     */
    void set_hw_latency_measurement_flags(hailo_latency_measurement_flags_t latency);

    /**
     * Configures the InferModel object. Also checks the validity of the configuration's formats.
     *
     * @return Upon success, returns Expected of ConfiguredInferModel, which can be used to perform an asynchronous inference.
     *  Otherwise, returns Unexpected of ::hailo_status error.
     * @note InferModel can be configured once.
     */
    Expected<ConfiguredInferModel> configure();

    /**
     * Returns the single input's InferStream object.
     *
     * @return Upon success, returns Expected of the single input's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
     * @note If InferModel has multiple inputs, will return ::HAILO_INVALID_OPERATION.
     *  In that case - use input(const std::string &name) instead.
     */
    Expected<InferStream> input();

    /**
     * Returns the single output's InferStream object.
     *
     * @return Upon success, returns Expected of the single output's InferStream object. Otherwise, returns Unexpected of ::hailo_status error.
     * @note If InferModel has multiple outputs, will return ::HAILO_INVALID_OPERATION.
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
     * @return A constant reference to the vector of input InferStream objects, each representing an input edge.
     */
    const std::vector<InferStream> &inputs() const;

    /**
     * @return A constant reference to the vector of output InferStream objects, each representing an output edge.
     */
    const std::vector<InferStream> &outputs() const;

    /**
     * @return A constant reference to a vector of strings, each representing the name of an input stream.
     */
    const std::vector<std::string> &get_input_names() const;

    /**
     * @return A constant reference to a vector of strings, each representing the name of an output stream.
     */
    const std::vector<std::string> &get_output_names() const;

    InferModel(InferModel &&);

    Expected<ConfiguredInferModel> configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
        std::shared_ptr<ConfiguredNetworkGroup> net_group = nullptr);

private:
    friend class VDevice;

    InferModel(VDevice &vdevice, Hef &&hef, std::unordered_map<std::string, InferStream> &&inputs,
        std::unordered_map<std::string, InferStream> &&outputs);

    std::reference_wrapper<VDevice> m_vdevice;
    Hef m_hef;
    std::unordered_map<std::string, InferStream> m_inputs;
    std::unordered_map<std::string, InferStream> m_outputs;
    std::vector<InferStream> m_inputs_vector;
    std::vector<InferStream> m_outputs_vector;
    std::vector<std::string> m_input_names;
    std::vector<std::string> m_output_names;
    ConfigureNetworkParams m_config_params;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_INFER_HPP_ */

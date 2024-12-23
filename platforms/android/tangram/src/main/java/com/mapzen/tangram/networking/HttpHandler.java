package com.mapzen.tangram.networking;

//import com.mapzen.tangram.MapController;
//import com.mapzen.tangram.MapView;

import java.io.IOException;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/**
 * {@code HttpHandler} interface for handling network requests for map resources,
 * it can be implemented to provide non-default http network request or caching behavior.
 * To use client implemented HttpHandler provide one during map initialization
 * {@link MapView#getMapAsync(MapView.MapReadyCallback, HttpHandler)}
 */
public interface HttpHandler {
    /**
     * Begin an HTTP request
     * @param url URL for the requested resource
     * @param headers HTTP headers, separated by \r\n
     * @param payload if present HTTP POST payload (otherwise GET assumed)
     * @param cb Callback for handling request result
     * @return identifier associated with this network request, used to canceling the request
     */
    Object startRequest(@NonNull final String url, final String headers, final String payload, @NonNull final Callback cb);

    /**
     * Cancel an HTTP request
     * @param request an identifier for the request to be cancelled
     */
    void cancelRequest(final Object request);

    /**
     * Cancel all running and queued requests
     */
    void cancelAllRequests();

    /**
     * {@code Callback}
     * Passes network responses from underlying implementation to be processed internally
     */
    interface Callback {
        /**
         * Network request failed response
         * @param e Exception representing the failed network request
         */
        void onFailure(@Nullable final IOException e);

        /**
         * Called when the HTTP response was successfully returned by the remote server
         * @param code Status code returned from the network response
         * @param body Data comprising the body of the network response
         */
        void onResponse(final int code, @Nullable final byte[] body);

        /**
         * Called when the request could not be executed due to cancellation
         */
        void onCancel();
    }
}


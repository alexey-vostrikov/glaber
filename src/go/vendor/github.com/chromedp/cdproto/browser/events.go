package browser

// Code generated by cdproto-gen. DO NOT EDIT.

import (
	"github.com/chromedp/cdproto/cdp"
)

// EventDownloadWillBegin fired when page is about to start a download.
//
// See: https://chromedevtools.github.io/devtools-protocol/tot/Browser#event-downloadWillBegin
type EventDownloadWillBegin struct {
	FrameID           cdp.FrameID `json:"frameId"`           // Id of the frame that caused the download to begin.
	GUID              string      `json:"guid"`              // Global unique identifier of the download.
	URL               string      `json:"url"`               // URL of the resource being downloaded.
	SuggestedFilename string      `json:"suggestedFilename"` // Suggested file name of the resource (the actual name of the file saved on disk may differ).
}

// EventDownloadProgress fired when download makes progress. Last call has
// |done| == true.
//
// See: https://chromedevtools.github.io/devtools-protocol/tot/Browser#event-downloadProgress
type EventDownloadProgress struct {
	GUID          string                `json:"guid"`          // Global unique identifier of the download.
	TotalBytes    float64               `json:"totalBytes"`    // Total expected bytes to download.
	ReceivedBytes float64               `json:"receivedBytes"` // Total bytes received.
	State         DownloadProgressState `json:"state"`         // Download status.
}

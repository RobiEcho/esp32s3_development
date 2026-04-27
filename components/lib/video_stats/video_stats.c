#include "video_stats.h"
#include "esp_heap_caps.h"

video_stats_t* video_stats_create(void)
{
    video_stats_t* stats = heap_caps_malloc(sizeof(video_stats_t), MALLOC_CAP_8BIT);
    if (!stats) return NULL;
    
    stats->frame_decoded = 0;
    stats->frame_displayed = 0;
    stats->frame_dropped = 0;
    stats->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    
    return stats;
}

void video_stats_destroy(video_stats_t* stats)
{
    if (stats) {
        heap_caps_free(stats);
    }
}

void video_stats_reset(video_stats_t* stats)
{
    if (!stats) return;
    
    portENTER_CRITICAL(&stats->lock);
    stats->frame_decoded = 0;
    stats->frame_displayed = 0;
    stats->frame_dropped = 0;
    portEXIT_CRITICAL(&stats->lock);
}

void video_stats_inc_decoded(video_stats_t* stats)
{
    if (!stats) return;
    
    portENTER_CRITICAL(&stats->lock);
    stats->frame_decoded++;
    portEXIT_CRITICAL(&stats->lock);
}

void video_stats_inc_displayed(video_stats_t* stats)
{
    if (!stats) return;
    
    portENTER_CRITICAL(&stats->lock);
    stats->frame_displayed++;
    portEXIT_CRITICAL(&stats->lock);
}

void video_stats_inc_dropped(video_stats_t* stats)
{
    if (!stats) return;
    
    portENTER_CRITICAL(&stats->lock);
    stats->frame_dropped++;
    portEXIT_CRITICAL(&stats->lock);
}

void video_stats_get(video_stats_t* stats, uint32_t* decoded, uint32_t* displayed, uint32_t* dropped)
{
    if (!stats) return;
    
    portENTER_CRITICAL(&stats->lock);
    if (decoded) *decoded = stats->frame_decoded;
    if (displayed) *displayed = stats->frame_displayed;
    if (dropped) *dropped = stats->frame_dropped;
    portEXIT_CRITICAL(&stats->lock);
}

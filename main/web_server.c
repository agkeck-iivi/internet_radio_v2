#include "web_server.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "station_data.h"
#include <stdlib.h>
#include <sys/param.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/* Handler for GET /api/stations */
static esp_err_t api_stations_get_handler(httpd_req_t *req) {
  char *json_str = get_stations_json();
  if (json_str == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  return ESP_OK;
}

/* Handler for POST /api/stations */
static esp_err_t api_stations_post_handler(httpd_req_t *req) {
  int total_len = req->content_len;
  int cur_len = 0;
  // char *buf = ((server_context_t *)(req->user_ctx))->scratch; // defined in
  // context
  // Simple malloc for now to handle arbitrary size
  char *content = malloc(total_len + 1);

  if (!content) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int received = 0;
  while (cur_len < total_len) {
    received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      free(content);
      return ESP_FAIL;
    }
    cur_len += received;
  }
  content[total_len] = '\0';

  if (update_stations_from_json(content) == 0) {
    save_station_data();
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_send_500(req);
  }

  free(content);
  return ESP_OK;
}

/* Handler for GET / (Root) - Landing Page */
static esp_err_t root_get_handler(httpd_req_t *req) {
  const char *html_response =
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<style>body{font-family:sans-serif;margin:20px;text-align:center;"
      "background:#f0f2f5;}"
      ".btn{display:inline-block;padding:15px "
      "30px;margin:10px;background:#007bff;color:white;text-decoration:none;"
      "border-radius:5px;font-size:1.2em;}"
      ".btn:hover{background:#0056b3;}</style>"
      "<title>Radio Manager</title></head>"
      "<body><h1>Internet Radio Manager</h1>"
      "<a href='/stations' class='btn'>Edit Stations</a><br>"
      "<a href='/config' class='btn'>Configuration</a>"
      "</body></html>";

  httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* Handler for GET /stations - Station Editor */
static esp_err_t stations_page_handler(httpd_req_t *req) {
  const char *html_response =
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<title>Edit Stations</title>"
      "<style>"
      "body{font-family:sans-serif;background:#f0f2f5;padding:10px;}"
      ".grid-container{display:grid;grid-template-columns:2em 5em 12em 1fr 6em "
      "3em;gap:5px;align-items:center;min-width:650px;}"
      "@media(max-width: 650px) { "
      ".grid-container{display:flex;flex-direction:column;min-width:auto;} "
      ".header-row{display:none;} "
      ".station-row{display:flex;flex-wrap:wrap;gap:5px;} }"
      ".header-row{display:contents;font-weight:bold;color:#555;}"
      ".header-row span{padding:5px 0;border-bottom:2px solid "
      "#ddd;margin-bottom:5px;text-align:left;}"
      ".station-row{display:contents;}"
      "@media(max-width: 650px) { "
      ".station-row{background:white;padding:10px;border-radius:4px;box-shadow:"
      "0 1px 2px "
      "rgba(0,0,0,0.1);display:flex;flex-direction:row;align-items:center;} }"
      "input,select{width:100%;padding:5px;border:1px solid "
      "#ccc;border-radius:3px;font-size:0.9em;box-sizing:border-box;}"
      ".inp-call{width:100%;}"
      ".inp-orig{width:100%;}"
      ".inp-uri{width:100%;font-family:monospace;}"
      ".btn{padding:5px "
      "10px;background:#28a745;color:white;border:none;border-radius:3px;"
      "cursor:pointer;font-size:0.9em;}"
      ".btn-del{background:#dc3545;padding:2px "
      "8px;font-size:0.8em;height:100%;}"
      ".homelink{display:inline-block;margin-bottom:10px;color:#007bff;text-"
      "decoration:none;}"
      ".controls{margin-top:20px;}"
      ".handle{cursor:grab;font-size:1.4em;color:#888;user-select:none;text-"
      "align:center;}"
      ".handle:active{cursor:grabbing;color:#000;}"
      "</style></head><body>"
      "<a href='/' class='homelink'>&larr; Home</a>"
      "<h3>Edit Stations</h3>"
      "<div id='wrapper' style='overflow-x:auto;'>"
      "<div class='grid-container'>"
      "  <div "
      "class='header-row'><span></span><span>Call</span><span>Origin</"
      "span><span>URI</span><span>Type</span><span>Del</span></div>"
      "  <div id='container' style='display:contents;'></div>"
      "</div>"
      "</div>"
      "<div class='controls'>"
      "<button class='btn' onclick='addStation()'>+ Add Station</button> "
      "<button class='btn' onclick='saveStations()'>Save Changes</button>"
      "</div>"
      "<script>"
      "let stations=[];"
      "let dragSrcIx = null;"
      "async function fetchStations(){"
      "  const r=await fetch('/api/stations');stations=await r.json();render();"
      "}"
      "function render(){"
      "  const c=document.getElementById('container');c.innerHTML='';"
      "  stations.forEach((s,i)=>{"
      "    const div=document.createElement('div');div.className='station-row';"
      "    div.innerHTML=`"
      "      <div class='handle' draggable='true' "
      "ondragstart='dragStart(event,${i})' ondragover='dragOver(event)' "
      "ondrop='drop(event,${i})'>&#9776;</div>"
      "      <div><input class='inp-call' value='${s.call_sign}' "
      "onchange='stations[${i}].call_sign=this.value' maxlength='4'></div>"
      "      <div><input class='inp-orig' value='${s.origin}' "
      "onchange='stations[${i}].origin=this.value' maxlength='20'></div>"
      "      <div><input class='inp-uri' value='${s.uri}' "
      "onchange='stations[${i}].uri=this.value'></div>"
      "      <div><select class='inp-codec' "
      "onchange='stations[${i}].codec=parseInt(this.value)'>"
      "        <option value='0' ${s.codec==0?'selected':''}>MP3</option>"
      "        <option value='1' ${s.codec==1?'selected':''}>AAC</option>"
      "        <option value='2' ${s.codec==2?'selected':''}>OGG</option>"
      "        <option value='3' ${s.codec==3?'selected':''}>FLAC</option>"
      "      </select></div>"
      "      <div style='text-align:center'><button class='btn btn-del' "
      "onclick='removeStation(${i})'>X</button></div>`;"
      "    c.appendChild(div);"
      "  });"
      "}"
      "function "
      "dragStart(e,i){dragSrcIx=i;e.dataTransfer.effectAllowed='move';e."
      "dataTransfer.setData('text/plain',i);}"
      "function "
      "dragOver(e){e.preventDefault();e.dataTransfer.dropEffect='move';return "
      "false;}"
      "function drop(e,i){"
      "  e.stopPropagation();"
      "  if(dragSrcIx!==null && dragSrcIx!=i){"
      "    const item=stations[dragSrcIx];"
      "    stations.splice(dragSrcIx,1);"
      "    /* Adjust index if we removed an item before the target */"
      "    /* Actually, standard splice logic: remove then insert. If removing "
      "from before, i shifts. */"
      "    /* But simpler: remove item, then insert at 'i'. However, if "
      "dragSrc < i, i decreases by 1 after remove. */"
      "    /* Let's recalculate target index carefully. */"
      "    /* Simplified swap logic is better for UX usually, but reorder is "
      "move. */"
      "    /* Let's just do a move. */"
      "    let target = i;"
      "    /* if source was before target, target index has shifted down by 1 "
      "in the intermediate array (minus source) */"
      "    if (dragSrcIx < i) target--;"
      "    stations.splice(target, 0, item);"
      "    render();"
      "  }"
      "  return false;"
      "}"
      "function "
      "addStation(){stations.push({call_sign:'',origin:'',uri:'',codec:1});"
      "render();}"
      "function "
      "removeStation(i){if(confirm('Delete?')){stations.splice(i,1);render();}}"
      "async function saveStations(){"
      "  await "
      "fetch('/api/"
      "stations',{method:'POST',headers:{'Content-Type':'application/"
      "json'},body:JSON.stringify(stations)});"
      "  alert('Saved!');"
      "}"
      "fetchStations();"
      "</script></body></html>";

  httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* Handler for GET /config - Configuration Placeholder */
static esp_err_t config_page_handler(httpd_req_t *req) {
  const char *html_response =
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<title>Configuration</title></head>"
      "<body><h1>Configuration</h1><p>Not implemented yet.</p>"
      "<a href='/'>Back to Home</a></body></html>";
  httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static const httpd_uri_t api_stations_get = {.uri = "/api/stations",
                                             .method = HTTP_GET,
                                             .handler =
                                                 api_stations_get_handler,
                                             .user_ctx = NULL};

static const httpd_uri_t api_stations_post = {.uri = "/api/stations",
                                              .method = HTTP_POST,
                                              .handler =
                                                  api_stations_post_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t root_get = {.uri = "/",
                                     .method = HTTP_GET,
                                     .handler = root_get_handler,
                                     .user_ctx = NULL};

static const httpd_uri_t stations_page_get = {.uri = "/stations",
                                              .method = HTTP_GET,
                                              .handler = stations_page_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t config_page_get = {.uri = "/config",
                                            .method = HTTP_GET,
                                            .handler = config_page_handler,
                                            .user_ctx = NULL};

void start_web_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192; // Increase stack size for JSON parsing if needed

  ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &api_stations_get);
    httpd_register_uri_handler(server, &api_stations_post);
    httpd_register_uri_handler(server, &root_get);
    httpd_register_uri_handler(server, &stations_page_get);
    httpd_register_uri_handler(server, &config_page_get);
  } else {
    ESP_LOGE(TAG, "Error starting server!");
  }
}

void stop_web_server(void) {
  if (server) {
    httpd_stop(server);
  }
}

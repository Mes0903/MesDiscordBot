#include <dpp/dpp.h>
#include <dpp/fmt/format.h>
#include <dpp/message.h>
#include <dpp/nlohmann/json.hpp>
#include <iostream>
#include <sstream>
using json = nlohmann::json;

int main() {
    /* Setup the bot */
    json configdocument;
    std::ifstream configfile( "../config.json" );
    configfile >> configdocument;
    dpp::cluster bot( configdocument["token"] );

    /* Create command handler, and specify prefixes */
    dpp::commandhandler command_handler( &bot );
    /* Specifying a prefix of "/" tells the command handler it should also expect slash commands */
    command_handler.add_prefix( "." ).add_prefix( "/" );

    /* Message handler to look for a command called !button */
    bot.on_message_create( [&bot]( const dpp::message_create_t &event ) {
        if ( event.msg->content == "!button" ) {
            /* Create a message containing an action row, and a button within the action row. */
            bot.message_create(
                dpp::message( event.msg->channel_id, "this text has buttons" ).add_component( dpp::component().add_component( dpp::component().set_label( "你他媽再點" ).set_type( dpp::cot_button ).set_emoji( "😄" ).set_style( dpp::cos_danger ).set_id( "何宜謙好強==" ) ) ) );
        }
        else if ( event.msg->content == "!test" ) {
            bot.message_create( dpp::message( event.msg->channel_id, "Success!" ) );
        }
        else if ( event.msg->content == "!terry" ) {
            bot.message_create( dpp::message( event.msg->channel_id, "何宜謙好電......" ) );
        }
        else if ( event.msg->content == "!select" ) {
            /* Create a message containing an action row, and a select menu within the action row. */
            dpp::message m( event.msg->channel_id, "this text has a select menu" );
            m.add_component(
                dpp::component().add_component(
                    dpp::component().set_type( dpp::cot_selectmenu ).set_placeholder( "Pick something" ).add_select_option( dpp::select_option( "label1", "value1", "description1" ).set_emoji( "😄" ) ).add_select_option( dpp::select_option( "label2", "value2", "description2" ).set_emoji( "🙂" ) ).set_id( "myselid" ) ) );
            bot.message_create( m );
        }
    } );

    /* When a user clicks your button, the on_button_click event will fire,
     * containing the custom_id you defined in your button.
     */
    bot.on_button_click( [&bot]( const dpp::button_click_t &event ) {
        /* Button clicks are still interactions, and must be replied to in some form to
         * prevent the "this interaction has failed" message from Discord to the user.
         */
        event.reply( dpp::ir_channel_message_with_source, event.custom_id );
    } );

    bot.on_select_click( [&bot]( const dpp::select_click_t &event ) {
        /* Select clicks are still interactions, and must be replied to in some form to
         * prevent the "this interaction has failed" message from Discord to the user.
         */
        event.reply( dpp::ir_channel_message_with_source, "You clicked " + event.custom_id + " and chose: " + event.values[0] );
    } );

    bot.on_ready( [&bot, &command_handler]( const dpp::ready_t &event ) {
        std::cout << "Logged in as " << bot.me.username << '\n';

        command_handler.add_command(
            /* Command name */
            "ping",

            /* Parameters */
            {
                { "testparameter", dpp::param_info( dpp::pt_string, true, "Optional test parameter" ) } },

            /* Command handler */
            [&command_handler]( const std::string &command, const dpp::parameter_list_t &parameters, dpp::command_source src ) {
                std::string got_param;
                if ( !parameters.empty() ) {
                    got_param = std::get<std::string>( parameters[0].second );
                }
                command_handler.reply( dpp::message( "Pong! -> " + got_param ), src );
            },

            /* Command description */
            "A test ping command" );

        command_handler.add_command(
            /* Command name */
            "terry",

            /* Parameters */
            {},

            /* Command handler */
            [&command_handler]( const std::string &command, const dpp::parameter_list_t &parameters, dpp::command_source src ) {
                command_handler.reply( dpp::message( "何宜謙太強了吧......" ), src );
            },

            /* Command description */
            "==" );
    } );

    bot.start( false );

    return 0;
}
